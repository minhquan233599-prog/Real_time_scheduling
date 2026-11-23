#include "kernel/types.h"
#include "user/user.h"

#define CAL_MIN_TICKS  20
#define CAL_MAX_BLOCKS 1048576
#define CAL_SAMPLES    3

struct calibration {
  int blocks;
  int minimum;
  int maximum;
  int total;
};

// Vòng lặp dùng chung, không đặt static vì rt_compare.c cần gọi.
void
run_blocks(int blocks)
{
  volatile uint value = 1;
  for (int block = 0; block < blocks; block++) {
    for (int n = 0; n < 10000; n++)
      value = value * 1664525U + 1013904223U;
  }
}

// Đọc đủ thông điệp qua pipe.
static int
read_full(int fd, void *buffer, int size)
{
  char *p = buffer;
  int received = 0;
  while (received < size) {
    int n = read(fd, p + received, size - received);
    if (n <= 0)
      return -1;
    received += n;
  }
  return 0;
}

// Đo thời gian trôi qua, không phải thời gian CPU chính xác.
static int
measure_blocks(int blocks)
{
  uint start = (uint)uptime();
  run_blocks(blocks);
  return (int)((uint)uptime() - start);
}

// Trả 0 khi thành công, -1 khi thất bại; kết quả qua hai con trỏ.
int
calibrate(int *measured_blocks, int *maximum_ticks)
{
  int channel[2];
  if (pipe(channel) < 0)
    return -1;
  int pid = fork();
  if (pid < 0) {
    close(channel[0]);
    close(channel[1]);
    return -1;
  }
  if (pid == 0) {
    close(channel[0]);
    // Đăng ký RT để chạy trên CPU RT với budget đủ lớn cho phép đo.
    if (sched_setdeadline(100000, 10000) != 0)
      exit(1);
    struct calibration c;
    c.blocks = 1024;
    c.minimum = -1;
    c.maximum = 0;
    c.total = 0;
    for (;;) {
      int elapsed = measure_blocks(c.blocks);
      if (elapsed < 0)
        exit(2);
      if (elapsed >= CAL_MIN_TICKS)
        break;
      if (c.blocks >= CAL_MAX_BLOCKS)
        exit(2);
      c.blocks *= 2;
    }
    // Đo ba mẫu cùng lượng công việc; không in giữa các mẫu.
    for (int i = 0; i < CAL_SAMPLES; i++) {
      int elapsed = measure_blocks(c.blocks);
      if (elapsed <= 0)
        exit(2);
      if (c.minimum < 0 || elapsed < c.minimum)
        c.minimum = elapsed;
      if (elapsed > c.maximum)
        c.maximum = elapsed;
      c.total += elapsed;
    }
    int sent = write(channel[1], &c, sizeof(c));
    close(channel[1]);
    exit(sent == sizeof(c) ? 0 : 3);
  }
  close(channel[1]);
  struct calibration c;
  int received = read_full(channel[0], &c, sizeof(c));
  close(channel[0]);
  int status = -1;
  int got = wait(&status);
  // Con thoát hoàn toàn để giải phóng đăng ký RT trước workload.
  if (received < 0 || got != pid || status != 0)
    return -1;
  if (c.blocks <= 0 || c.minimum <= 0 || c.maximum < c.minimum)
    return -1;
  printf("CAL: blocks=%d samples=%d total_ticks=%d min=%d max=%d\n", c.blocks,
         CAL_SAMPLES, c.total, c.minimum, c.maximum);
  if (c.maximum - c.minimum > 2) {
    printf("CALIBRATION: FAIL (unstable measurements)\n");
    return -1;
  }
  *measured_blocks = c.blocks;
  *maximum_ticks = c.maximum;
  printf("CALIBRATION: PASS\n");
  return 0;
}
