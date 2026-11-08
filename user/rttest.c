#include "kernel/types.h"
#include "user/user.h"
#include "kernel/riscv.h"

// Tra 1 neu ket qua dung nhu mong doi.

static int
expect_rt(const char *label, int period, int runtime, int expected)
{
  int actual = sched_setdeadline(period, runtime);
  printf("  %s: T=%d C=%d expected=%d actual=%d [%s]\n", label, period, runtime,
         expected, actual, actual == expected ? "PASS" : "FAIL");
  return actual == expected;
}

// Chi goi khi tien trinh hien tai co dung mot tien trinh con.
static int
wait_ok(int pid)
{
  int status = -1;
  int got = wait(&status);
  if (got != pid || status != 0) {
    printf("  child failed: expected_pid=%d got=%d status=%d\n", pid, got,
           status);
    return 0;
  }
  return 1;
}

// Bat dau test

static int
test01(void)
{
  return expect_rt("valid", 5, 2, 0);
}

static int
test02(void)
{
  int ok = expect_rt("zero period", 0, 1, -1);
  ok &= expect_rt("zero runtime", 5, 0, -1);
  return ok;
}

static int
test03(void)
{
  return expect_rt("runtime > period", 5, 6, -1);
}

static int
test04(void)
{
  return expect_rt("full utilization", 5, 5, 0);
}

// Tien trinh hien tai la B; tien trinh con la A.

// ready: A bao da dang ky xong.

// hold: A cho EOF; B chi dong dau ghi sau khi thu xong.

static int
test05(void)
{
  int ready[2], hold[2];
  if (pipe(ready) < 0) {
    printf("  pipe ready failed\n");
    return 0;
  }

  if (pipe(hold) < 0) {
    close(ready[0]);
    close(ready[1]);
    printf("  pipe hold failed\n");
    return 0;
  }

  int pid = fork();
  if (pid < 0) {
    close(ready[0]);
    close(ready[1]);
    close(hold[0]);
    close(hold[1]);
    printf("  fork A failed\n");
    return 0;
  }

  if (pid == 0) {
    // A: dong cac dau pipe khong dung.
    close(ready[0]);
    close(hold[1]);
    int ok = expect_rt("A registers", 5, 3, 0);
    char flag = ok ? '1' : '0';
    if (write(ready[1], &flag, 1) != 1)
      ok = 0;
    close(ready[1]);

    // A van song va giu dang ky 3/5 trong luc B thu.
    // read tra 0 khi B dong hold[1].
    char unused;
    if (read(hold[0], &unused, 1) != 0)
      ok = 0;
    close(hold[0]);
    exit(ok ? 0 : 1);
  }

  // B.
  close(ready[1]);
  close(hold[0]);
  char flag = '0';
  int ok = (read(ready[0], &flag, 1) == 1 && flag == '1');
  close(ready[0]);
  if (ok) {
    ok &= expect_rt("B overload", 5, 3, -2);
    // Van thuc hien buoc nay ke ca buoc tren sai.
    ok &= expect_rt("B retry", 5, 2, 0);
  } else {
    printf("  A setup failed\n");
  }
  // Cho phep A thoat, roi thu hoi A.
  close(hold[1]);
  ok &= wait_ok(pid);
  return ok;
}

static int
test06(void)
{
  int pid = fork();
  if (pid < 0) {
    printf("  fork A failed\n");
    return 0;
  }
  if (pid == 0) {
    int ok = expect_rt("A before exit", 5, 5, 0);
    exit(ok ? 0 : 1);
  }
  // B chi dang ky sau khi A da thoat va duoc thu hoi.
  if (!wait_ok(pid))
    return 0;
  return expect_rt("B after A exit", 5, 5, 0);
}

struct test_case {

  const char *name;

  int (*run)(void);
};

static int
countfree(void)
{
  int n = 0;
  uint64 sz0 = (uint64)sbrk(0);

  while (sbrk(PGSIZE) != SBRK_ERROR)
    n++;

  sbrk(-((uint64)sbrk(0) - sz0));
  return n;
}


int
main(void)
{
  struct test_case cases[] = {
    {"valid registration", test01},     {"zero arguments", test02},
    {"runtime exceeds period", test03}, {"utilization equals one", test04},
    {"overload and retry", test05},     {"release after exit", test06},
  };
  int total = sizeof(cases) / sizeof(cases[0]);
  int passed = 0;
  printf("RT syscall tests - Sprint 3\n");

  int pages_before = countfree();

  for (int i = 0; i < total; i++) {
    printf("\nCASE %d: %s\n", i + 1, cases[i].name);
    // Moi ca chay trong mot tien trinh rieng.
    // Tien trinh dieu phoi khong dang ky RT.
    int pid = fork();
    if (pid < 0) {
      printf("CASE %d: FAIL (fork failed)\n", i + 1);
      continue;
    }
    if (pid == 0) {
      int ok = cases[i].run();
      exit(ok ? 0 : 1);
    }
    int ok = wait_ok(pid);
    if (ok)
      passed++;
    printf("CASE %d: %s\n", i + 1, ok ? "PASS" : "FAIL");
  }
  printf("\nRESULT: %d/%d PASS\n", passed, total);

  int pages_after = countfree();
  int pages_ok = pages_after >= pages_before;

  printf("FREE PAGES: before=%d after=%d [%s]\n",
       pages_before, pages_after, pages_ok ? "PASS" : "FAIL");
 
  exit(passed == total && pages_ok ? 0 : 1);
}