#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
check(const char *name, int period, int runtime, int expected)
{
  int actual = sched_setdeadline(period, runtime);

  printf("%s: expected=%d actual=%d %s\n",
         name,
         expected,
         actual,
         actual == expected ? "PASS" : "FAIL");
}

int
main(void)
{
  printf("RT syscall smoke test\n");

  check("valid registration", 5, 2, 0);
  check("zero period",       0, 1, -1);
  check("zero runtime",      5, 0, -1);
  check("runtime > period",  5, 6, -1);

  // Đăng ký lại: thay đăng ký 2/5 cũ bằng 5/5.
  check("replace registration", 5, 5, 0);

  exit(0);
}