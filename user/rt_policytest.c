#include "kernel/types.h"
#include "kernel/schedpolicy.h"
#include "user/user.h"

static int
expect(char *name, int actual, int expected)
{
  int ok = actual == expected;
  printf("%s: expected=%d actual=%d [%s]\n", name, expected, actual,
         ok ? "PASS" : "FAIL");
  return ok;
}

static int
check_busy(int policy)
{
  int ready[2], done[2];
  if (pipe(ready) < 0)
    return 0;
  if (pipe(done) < 0) {
    close(ready[0]);
    close(ready[1]);
    return 0;
  }

  int pid = fork();
  if (pid < 0) {
    close(ready[0]);
    close(ready[1]);
    close(done[0]);
    close(done[1]);
    return 0;
  }

  if (pid == 0) {
    close(ready[0]);
    close(done[1]);
    // Tải nhỏ để kiểm tra giao diện, không phải benchmark.
    char mark = sched_setdeadline(20, 1) == 0 ? 'R' : 'E';
    if (write(ready[1], &mark, 1) != 1)
      exit(1);
    close(ready[1]);
    if (mark != 'R') {
      close(done[0]);
      exit(1);
    }
    // Giữ đăng ký RT trong khi cha thử đổi policy.
    int received = read(done[0], &mark, 1);
    close(done[0]);
    exit(received == 1 ? 0 : 1);
  }

  close(ready[1]);
  close(done[0]);
  char mark = 0;
  int registered = read(ready[0], &mark, 1) == 1 && mark == 'R';
  close(ready[0]);
  int ok = expect("child registration", registered, 1);

  if (registered) {
    int other = policy == SCHED_EDF ? SCHED_RM : SCHED_EDF;
    // Gọi từng kiểm tra riêng để không bỏ qua ca sau khi ca trước lỗi.
    ok &= expect("same policy while RT exists", sched_setpolicy(policy), 0);
    ok &= expect("reject change while RT exists", sched_setpolicy(other), -2);
    ok &= expect("policy unchanged", sched_getpolicy(), policy);
    ok &= expect("invalid while RT exists", sched_setpolicy(99), -1);
    mark = 'X';
    ok &= expect("release child", write(done[1], &mark, 1), 1);
  }

  // Đóng pipe cũng giúp con thoát nếu cha gặp lỗi setup.
  close(done[1]);
  int status = -1;
  int got = wait(&status);
  ok &= expect("wait child", got, pid);
  ok &= expect("child status", status, 0);
  return ok;
}

static int
run_once(void)
{
  int ok = 1;
  int policies[] = {SCHED_EDF, SCHED_RM};

  for (int i = 0; i < 2; i++) {
    int policy = policies[i];
    printf("POLICY=%s\n", policy == SCHED_EDF ? "EDF" : "RM");
    int rc = sched_setpolicy(policy);
    ok &= expect("select policy", rc, 0);
    if (rc != 0)
      return 0;

    ok &= expect("get policy", sched_getpolicy(), policy);
    ok &= expect("negative policy", sched_setpolicy(-1), -1);
    ok &= expect("unknown policy", sched_setpolicy(99), -1);
    ok &= expect("unchanged after invalid", sched_getpolicy(), policy);
    ok &= check_busy(policy);

    // Sau wait, đăng ký của con đã được thu hồi và được phép đổi policy.
    int other = policy == SCHED_EDF ? SCHED_RM : SCHED_EDF;
    ok &= expect("change after child exits", sched_setpolicy(other), 0);
    ok &= expect("get after change", sched_getpolicy(), other);
  }
  return ok;
}

int
main(int argc, char *argv[])
{
  int runs = 1;
  if (argc == 2)
    runs = atoi(argv[1]);
  else if (argc != 1) {
    fprintf(2, "usage: rt_policytest [runs]\n");
    exit(1);
  }
  if (runs < 1 || runs > 100) {
    fprintf(2, "runs must be between 1 and 100\n");
    exit(1);
  }

  int original = sched_getpolicy();
  if (original != SCHED_EDF && original != SCHED_RM) {
    printf("POLICY SUITE: FAIL (invalid initial policy)\n");
    exit(1);
  }

  int passed = 0;
  for (int i = 0; i < runs; i++) {
    printf("\nPOLICY RUN %d/%d\n", i + 1, runs);
    int ok = run_once();
    passed += ok != 0;
    printf("POLICY RUN %d: %s\n", i + 1, ok ? "PASS" : "FAIL");
  }

  // Trả lại chính sách ban đầu để không ảnh hưởng test tiếp theo.
  int restored =
    expect("restore original policy", sched_setpolicy(original), 0);
  int all_ok = passed == runs && restored;
  printf("POLICY RESULT: %d/%d %s\n", passed, runs, all_ok ? "PASS" : "FAIL");
  exit(all_ok ? 0 : 1);
}