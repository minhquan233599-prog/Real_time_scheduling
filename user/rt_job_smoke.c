#include "kernel/types.h"
#include "user/user.h"

static int
expect(const char *name, int actual, int expected)
{
  int ok = actual == expected;

  printf("%s: expected=%d actual=%d [%s]\n",
         name, expected, actual, ok ? "PASS" : "FAIL");

  return ok;
}

int
main(void)
{
  int ok = 1;

  // Chưa đăng ký RT thì chưa được báo hoàn thành job.
  ok &= expect("done before registration", rt_job_done(), -1);

  // Kiểm tra tham số âm trước khi kích hoạt RT.
  ok &= expect("negative period", sched_setdeadline(-10, 2), -1);
  ok &= expect("negative runtime", sched_setdeadline(10, -2), -1);

  int rc = sched_setdeadline(20, 5);
  ok &= expect("register", rc, 0);

  if (rc != 0) {
    printf("RT JOB SMOKE: FAIL\n");
    exit(1);
  }

  // Gọi lại cùng cấu hình không được cấp lại budget.
  ok &= expect("same parameters", sched_setdeadline(20, 5), 0);

  // Bản tuần 4 này chưa cho phép đổi cấu hình đang hoạt động.
  ok &= expect("change active parameters",
               sched_setdeadline(30, 5), -3);

  for (int i = 0; i < 3; i++) {
    // Công việc ngắn để kiểm tra đường đi completion và release.
    for (volatile int n = 0; n < 10000; n++) {
    }

    rc = rt_job_done();

    // Lệnh này chỉ chạy sau khi syscall được phép trở về user.
    ok &= expect("job done and next release", rc, 0);
  }

  printf("RT JOB SMOKE: %s\n", ok ? "PASS" : "FAIL");
  exit(ok ? 0 : 1);
}