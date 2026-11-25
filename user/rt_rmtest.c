#include "kernel/types.h"
#include "kernel/schedpolicy.h"
#include "kernel/rttrace.h"
#include "user/user.h"

// Chỉ kiểm tra job đầu; job kế tiếp do rt_job_done tạo ra không thuộc ca test.
struct seen {
  int released, done, remaining, period, ticks;
};

static void
worker(int period, int budget, int duration, int notify, int gate)
{
  // Worker chạy sớm chỉ đăng ký sau khi worker chạy muộn đã vào pause.
  if (gate >= 0) {
    char token;
    int received = read(gate, &token, 1);
    close(gate);
    if (received != 1)
      exit(1);
  }
  int ok = sched_setdeadline(period, budget) == 0;
  if (notify >= 0) {
    char token = ok ? 'R' : 'F';
    if (write(notify, &token, 1) != 1)
      ok = 0;
    close(notify);
  }
  if (!ok)
    exit(1);

  // Worker đăng ký trước tạm ngủ để worker còn lại bắt đầu tính toán.
  // Sau khi thức, worker này không chờ I/O nữa trước job_done.
  if (notify >= 0)
    pause(4);

  // Đoạn workload chỉ tính toán và đọc đồng hồ.
  // duration là thời gian quan sát, không phải lượng CPU chính xác.
  uint start = (uint)uptime();
  volatile uint value = 1;
  do {
    for (int i = 0; i < 1000000; i++)
      value = value * 1664525U + 1013904223U;
  } while ((uint)uptime() - start < (uint)duration);

  if (rt_job_done() != 0)
    exit(2);
  exit(0);
}

static int
check_trace(int count, int lost, int a, int b, int tie)
{
  struct seen s[2] = {{0}, {0}};
  int ok = count > 0 && lost == 0;
  int observations = 0;
  int covered = 0;
  uint64 previous = 0;
  int late = tie ? 0 : 1;
  int early = 1 - late;

  for (int i = 0; i < count; i++) {
    struct rt_trace_event e;
    if (rt_trace_read(i, &e) != 1) {
      ok = 0;
      break;
    }
    if (e.seq != i || (i > 0 && e.tick < previous))
      ok = 0;
    previous = e.tick;
    if (e.type == RT_EV_NORMAL)
      continue;
    int k = e.pid == a ? 0 : (e.pid == b ? 1 : -1);
    if (k < 0) {
      ok = 0;
      continue;
    }
    if (e.cpu != 0)
      ok = 0;
    if (e.type == RT_EV_MISS)
      ok = 0;
    if (e.job_id != 1)
      continue;

    if (e.type == RT_EV_RELEASE) {
      s[k].released = 1;
      s[k].period = e.period;
    }
    s[k].remaining = e.remaining;
    if (e.type == RT_EV_TICK)
      s[k].ticks++;
    if (e.type == RT_EV_COMPLETE)
      s[k].done = 1;
    // Bỏ qua đường tiếp tục kernel: worker có thể vẫn đang ở trong pause().
    if (e.type != RT_EV_DISPATCH || e.aux == RT_TRACE_KERNEL)
      continue;

    // Chỉ bắt đầu kiểm tra khi worker muộn thực sự được chọn về workload RM.
    // Khi đó pause() đã kết thúc; worker này không chờ I/O nữa trước job_done().
    if (k == late && e.aux == RT_TRACE_RM && e.kernel_resume == 0 &&
        e.remaining > 0 && s[early].ticks > 0 && !s[early].done)
      covered = 1;
    if (e.aux != RT_TRACE_RM || e.remaining <= 0 || e.kernel_resume)
      ok = 0;

    // Hai worker không có chỗ chờ I/O trong đoạn đang được kiểm tra.
    if (covered && s[0].released && s[1].released && !s[0].done && !s[1].done &&
        s[0].remaining > 0 && s[1].remaining > 0) {
      int expected = tie ? (a < b ? a : b) : b;
      if (observations == 0 || e.pid != expected)
        printf("RM ORDER: tick=%lu expected=%d actual=%d [%s]\n", e.tick,
               expected, e.pid, e.pid == expected ? "PASS" : "FAIL");
      observations++;
      if (e.pid != expected)
        ok = 0;
    }
  }
  if (!covered || observations == 0 || !s[0].done || !s[1].done ||
      s[0].period != 80 || s[1].period != (tie ? 80 : 40))
    ok = 0;
  printf("RM TRACE: events=%d lost=%d observations=%d\n", count, lost,
         observations);
  printf("CHECK RM %s: %s\n", tie ? "tie-break PID" : "shorter period",
         ok ? "PASS" : "FAIL");
  return ok;
}

static int
run_case(int tie)
{
  int ready[2], gate[2];
  if (pipe(ready) < 0)
    return 0;
  if (pipe(gate) < 0) {
    close(ready[0]);
    close(ready[1]);
    return 0;
  }
  if (rt_trace_ctl(RT_TRACE_START) != 0) {
    close(ready[0]);
    close(ready[1]);
    close(gate[0]);
    close(gate[1]);
    return 0;
  }
  int pids[2] = {-1, -1};
  int ok = 1;
  for (int k = 0; k < 2; k++) {
    int pid = fork();
    if (pid < 0) {
      ok = 0;
      break;
    }
    if (pid == 0) {
      int late = k == (tie ? 0 : 1);
      close(ready[0]);
      close(gate[1]);
      if (late)
        close(gate[0]);
      else
        close(ready[1]);
      worker(k == 0 ? 80 : (tie ? 80 : 40), k == 0 ? 30 : 20, late ? 3 : 12,
             late ? ready[1] : -1, late ? -1 : gate[0]);
      exit(1);
    }
    pids[k] = pid;
  }
  close(ready[1]);
  close(gate[0]);
  char token = 0;
  if (ok && (read(ready[0], &token, 1) != 1 || token != 'R'))
    ok = 0;
  close(ready[0]);
  if (ok && write(gate[1], "G", 1) != 1)
    ok = 0;
  close(gate[1]);
  int a = pids[0], b = pids[1];
  if (!ok) {
    if (a > 0)
      kill(a);
    if (b > 0)
      kill(b);
  }
  int children = (a > 0) + (b > 0);
  int seen_a = 0, seen_b = 0;
  for (int i = 0; i < children; i++) {
    int status = -1;
    int pid = wait(&status);
    if (pid == a && !seen_a)
      seen_a = 1;
    else if (pid == b && !seen_b)
      seen_b = 1;
    else
      ok = 0;
    if (status != 0)
      ok = 0;
  }
  int count = rt_trace_ctl(RT_TRACE_STOP);
  int lost = rt_trace_ctl(RT_TRACE_LOST);
  int trace_ok = check_trace(count, lost, a, b, tie);
  printf("CHECK RM workers: %s\n", ok && seen_a && seen_b ? "PASS" : "FAIL");
  return ok && seen_a && seen_b && trace_ok;
}

// sbrk của baseline cấp trang ngay; mỗi trang xv6 có kích thước 4096 byte.
// Đo lượng trang có thể cấp cho user rồi trả lại toàn bộ vùng vừa xin.
static int
countfree(void)
{
  int pages = 0;
  while (sbrk(4096) != (char *)-1)
    pages++;
  if (pages > 0 && sbrk(-pages * 4096) == (char *)-1)
    return -1;
  return pages;
}

// Tác vụ ưu tiên cao: chu kỳ ngắn, hoàn thành nhanh rồi chờ kỳ mới.
static void
bench_high(void)
{
  if (sched_setdeadline(8, 2) != 0)
    exit(1);

  for (int job = 0; job < 3; job++) {
    for (volatile int n = 0; n < 20000; n++) {
    }
    if (rt_job_done() != 0)
      exit(1);
  }
  exit(0);
}

// Tác vụ ưu tiên thấp: chạy qua nhiều kỳ release của tác vụ trên.
static void
bench_low(void)
{
  if (sched_setdeadline(80, 30) != 0)
    exit(1);

  uint start = uptime();
  volatile uint value = 1;
  while ((uint)(uptime() - start) < 26) {
    // Tính toán lâu hơn giữa hai lần đọc đồng hồ để giảm sự kiện trace.
    for (int n = 0; n < 10000000; n++)
      value = value * 1664525U + 1013904223U;
  }
  exit(0);
}

// Trả độ trễ lớn nhất của một mẫu; -1 nếu lỗi hoặc thiếu tình huống.
static int
bench_once(void)
{
  if (rt_trace_ctl(RT_TRACE_START) != 0)
    return -1;

  int high = fork();
  if (high == 0)
    bench_high();

  int low = fork();
  if (low == 0)
    bench_low();

  int ok = high > 0 && low > 0;
  if (!ok) {
    if (high > 0)
      kill(high);
    if (low > 0)
      kill(low);
  }

  // Thu hồi hết các worker trước khi dừng trace.
  int children = (high > 0) + (low > 0);
  int seen_high = 0, seen_low = 0;
  for (int i = 0; i < children; i++) {
    int status = -1;
    int pid = wait(&status);
    if (pid == high && !seen_high)
      seen_high = 1;
    else if (pid == low && !seen_low)
      seen_low = 1;
    else
      ok = 0;
    if (status != 0)
      ok = 0;
  }

  int count = rt_trace_ctl(RT_TRACE_STOP);
  int lost = rt_trace_ctl(RT_TRACE_LOST);
  if (count <= 0 || lost != 0)
    ok = 0;

  uint64 previous = 0;
  uint64 low_tick = ~0ULL;
  uint64 release_tick = 0;
  int pending = 0;
  int observations = 0;
  int maximum = -1;

  for (int i = 0; i < count; i++) {
    struct rt_trace_event e;
    if (rt_trace_read(i, &e) != 1) {
      ok = 0;
      break;
    }
    if (e.seq != i || (i > 0 && e.tick < previous))
      ok = 0;
    previous = e.tick;

    int tracked = e.pid == high || e.pid == low;
    if (tracked && e.type != RT_EV_NORMAL && e.cpu != 0)
      ok = 0;
    if (tracked && e.type == RT_EV_MISS)
      ok = 0;

    // Timer vừa lấy mẫu low đang chạy và vẫn còn budget.
    if (e.pid == low && e.type == RT_EV_TICK)
      low_tick = e.remaining > 0 ? e.tick : ~0ULL;

    // High có T=8, ưu tiên cao hơn low có T=80.
    // Bỏ job đăng ký đầu vì đường syscall còn đang thực hiện.
    if (e.pid == high && e.type == RT_EV_RELEASE && e.job_id > 1 &&
        e.kernel_resume == 0 && e.remaining > 0 && low_tick == e.tick) {
      if (pending)
        ok = 0;
      pending = 1;
      release_tick = e.tick;
    }

    if (!pending || e.cpu != 0)
      continue;

    // Cho phép hoàn tất đường kernel trước khi xét workload.
    if (e.type == RT_EV_DISPATCH && e.aux == RT_TRACE_KERNEL)
      continue;

    if (e.type == RT_EV_DISPATCH) {
      int correct = e.aux == RT_TRACE_RM && e.pid == high &&
                    e.kernel_resume == 0 && e.remaining > 0;
      int delay = (int)(e.tick - release_tick);

      if (!correct)
        ok = 0;
      if (correct && delay > maximum)
        maximum = delay;

      observations++;
      pending = 0;
    } else if (e.type == RT_EV_NORMAL) {
      // High đã đủ điều kiện nhưng CPU 0 lại chọn tiến trình thường.
      ok = 0;
      pending = 0;
    }
  }

  if (pending || observations == 0 || !seen_high || !seen_low)
    ok = 0;

  printf("RM BENCH TRACE: events=%d lost=%d observations=%d [%s]\n", count,
         lost, observations, ok ? "PASS" : "FAIL");

  return ok ? maximum : -1;
}

static int
run_rm_benchmark(void)
{
  int original = sched_getpolicy();
  if ((original != SCHED_EDF && original != SCHED_RM) ||
      sched_setpolicy(SCHED_RM) != 0) {
    printf("RM BENCH RESULT: FAIL (policy)\n");
    return 0;
  }

  int ok = 1;
  int sum = 0;
  int minimum = 0;
  int maximum = 0;

  printf("RM BENCH: preemption delay, unit=tick, repetitions=5\n");

  for (int i = 0; i < 5; i++) {
    int delay = bench_once();
    if (delay < 0) {
      printf("RM BENCH SAMPLE %d: FAIL (invalid measurement)\n", i + 1);
      ok = 0;
      break;
    }

    sum += delay;
    if (i == 0 || delay < minimum)
      minimum = delay;
    if (i == 0 || delay > maximum)
      maximum = delay;

    printf("RM BENCH SAMPLE %d: max_delay=%d ticks\n", i + 1, delay);
  }

  int restored = sched_setpolicy(original) == 0;
  printf("CHECK restore policy: %s\n", restored ? "PASS" : "FAIL");

  if (ok) {
    // xv6 printf không hỗ trợ %02d, nên in riêng từng chữ số.
    int mean100 = sum * 100 / 5;
    printf("RM BENCH SUMMARY: n=5 mean=%d.%d%d ticks "
           "min=%d max=%d range=%d ticks\n",
           mean100 / 100, (mean100 % 100) / 10, mean100 % 10, minimum, maximum,
           maximum - minimum);
  }

  ok = ok && restored;
  printf("RM BENCH RESULT: %s\n", ok ? "PASS" : "FAIL");
  return ok;
}

int
main(int argc, char **argv)
{
  // Benchmark RM riêng: tự chọn policy và khôi phục sau khi đo.
  if (argc == 2 && strcmp(argv[1], "--bench") == 0)
    exit(run_rm_benchmark() ? 0 : 1);

  // Chế độ đo riêng để bao quanh toàn bộ phiên kiểm thử, không đổi policy.
  if (argc == 2 && strcmp(argv[1], "pages") == 0) {
    int pages = countfree();
    printf("PAGE SNAPSHOT: free=%d [%s]\n", pages, pages > 0 ? "PASS" : "FAIL");
    exit(pages > 0 ? 0 : 1);
  }
  int runs = 1;
  if (argc > 2) {
    fprintf(2, "usage: rt_rmtest [1..100|pages|--bench]\n");
    exit(1);
  }
  if (argc == 2) {
    runs = 0;
    for (int i = 0; argv[1][i]; i++) {
      if (argv[1][i] < '0' || argv[1][i] > '9' || runs > 100)
        exit(1);
      runs = runs * 10 + argv[1][i] - '0';
    }
  }
  if (runs < 1 || runs > 100)
    exit(1);
  int original = sched_getpolicy();
  if ((original != SCHED_EDF && original != SCHED_RM) ||
      sched_setpolicy(SCHED_RM) != 0) {
    printf("RM RESULT: FAIL (policy)\n");
    exit(1);
  }
  int before = countfree();
  if (before <= 0) {
    sched_setpolicy(original);
    printf("RM RESULT: FAIL (free-page measurement)\n");
    exit(1);
  }
  int passed = 0;
  for (int i = 0; i < runs; i++) {
    printf("\nRM RUN %d/%d\n", i + 1, runs);
    int order = run_case(0);
    int tie = run_case(1);
    int ok = order && tie;
    passed += ok;
    printf("RM RUN %d: %s\n", i + 1, ok ? "PASS" : "FAIL");
  }
  int restored = sched_setpolicy(original) == 0;
  printf("CHECK restore policy: %s\n", restored ? "PASS" : "FAIL");
  int after = countfree();
  int pages_ok = after >= before;
  printf("FREE PAGES: before=%d after=%d [%s]\n", before, after,
         pages_ok ? "PASS" : "FAIL");
  int all_ok = passed == runs && restored && pages_ok;
  printf("RM RESULT: %d/%d %s\n", passed, runs, all_ok ? "PASS" : "FAIL");
  exit(all_ok ? 0 : 1);
}