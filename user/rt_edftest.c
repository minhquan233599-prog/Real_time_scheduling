#include "kernel/types.h"
#include "kernel/rttrace.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define MAX_TRACKED 8

static int
report(const char *name, int ok)
{
  printf("CHECK %s: %s\n", name, ok ? "PASS" : "FAIL");
  return ok;
}

// Tạo một tiến trình thực hiện ba lần báo hoàn thành job.
static int
spawn_periodic(int period)
{
  int pid = fork();

  if (pid != 0)
    return pid;

  if (sched_setdeadline(period, 2) != 0)
    exit(1);

  for (int job = 0; job < 3; job++) {
    // Workload ngắn; không dùng vòng lặp này để đo CPU time chính xác.
    for (volatile int n = 0; n < 20000; n++) {
    }

    // Lệnh chỉ trở về user khi job kỳ sau được phép chạy.
    if (rt_job_done() != 0)
      exit(1);
  }

  exit(0);
}

// Chờ một tiến trình con và kiểm tra mã thoát.
static int
wait_success(void)
{
  int status = -1;
  int pid = wait(&status);

  return pid > 0 && status == 0;
}

// Kiểm tra lần chọn đầu tiên sau khi hai job cùng release.
// equal_deadline = 0: kiểm tra deadline khác nhau.
// equal_deadline = 1: kiểm tra tie-break bằng PID.
static int
test_edf_order_once(int period_a, int period_b, int equal_deadline)
{
  const char *name =
    equal_deadline ? "EDF tie-break by PID" : "EDF earliest deadline";

  if (rt_trace_ctl(RT_TRACE_START) != 0) {
    printf("CHECK %s: FAIL (cannot start trace)\n", name);
    return 0;
  }

  int workers_ok = 1;
  int children = 0;

  int pid_a = spawn_periodic(period_a);
  if (pid_a < 0)
    workers_ok = 0;
  else
    children++;

  int pid_b = spawn_periodic(period_b);
  if (pid_b < 0)
    workers_ok = 0;
  else
    children++;

  // Luôn thu hồi hết các con đã tạo thành công.
  for (int i = 0; i < children; i++) {
    if (!wait_success())
      workers_ok = 0;
  }

  int count = rt_trace_ctl(RT_TRACE_STOP);
  int lost = rt_trace_ctl(RT_TRACE_LOST);

  int trace_ok = count > 0 && lost == 0;
  int order_ok = 1;
  int comparisons = 0;

  // Theo dõi hai release xảy ra trong cùng tick.
  uint64 group_tick = 0;
  int have_a = 0;
  int have_b = 0;

  uint64 deadline_a = 0;
  uint64 deadline_b = 0;
  uint64 job_a = 0;
  uint64 job_b = 0;

  struct rt_trace_event e;

  if (count >= 0) {
    for (int i = 0; i < count; i++) {
      if (rt_trace_read(i, &e) != 1) {
        trace_ok = 0;
        break;
      }

      if (e.seq != i)
        trace_ok = 0;

      // Không ghép các release thuộc hai tick khác nhau.
      if (e.tick != group_tick) {
        group_tick = e.tick;
        have_a = 0;
        have_b = 0;
      }

      if (e.type == RT_EV_RELEASE) {
        // Chỉ xét job kỳ sau đang chờ ở điểm trở về user.
        // Bỏ lần đăng ký đầu vì syscall đăng ký còn ở trong kernel.
        if (e.job_id <= 1 || e.kernel_resume != 0)
          continue;

        if (e.cpu != 0 || e.remaining <= 0 ||
            e.deadline != e.release + (uint64)e.period || e.release != e.tick) {
          trace_ok = 0;
          continue;
        }

        if (e.pid == pid_a) {
          have_a = 1;
          deadline_a = e.deadline;
          job_a = e.job_id;
        } else if (e.pid == pid_b) {
          have_b = 1;
          deadline_b = e.deadline;
          job_b = e.job_id;
        }

        continue;
      }

      if (e.type != RT_EV_DISPATCH)
        continue;

      if (e.aux == RT_TRACE_EDF && have_a && have_b) {
        int suitable;

        if (equal_deadline)
          suitable = deadline_a == deadline_b;
        else
          suitable = deadline_a != deadline_b;

        if (suitable) {
          int expected;

          // Oracle: deadline nhỏ hơn thắng.
          // Nếu deadline bằng nhau, PID nhỏ hơn thắng.
          if (deadline_a < deadline_b ||
              (deadline_a == deadline_b && pid_a < pid_b))
            expected = pid_a;
          else
            expected = pid_b;

          uint64 expected_job = expected == pid_a ? job_a : job_b;

          uint64 expected_deadline =
            expected == pid_a ? deadline_a : deadline_b;

          int correct = e.pid == expected && e.job_id == expected_job &&
                        e.deadline == expected_deadline && e.cpu == 0 &&
                        e.kernel_resume == 0 && e.remaining > 0;

          comparisons++;

          if (!correct)
            order_ok = 0;

          printf("ORDER tick=%d A=%d dA=%d B=%d dB=%d "
                 "expected=%d actual=%d [%s]\n",
                 (int)e.tick, pid_a, (int)deadline_a, pid_b, (int)deadline_b,
                 expected, e.pid, correct ? "PASS" : "FAIL");
        }
      }

      // Chỉ xét lần dispatch đầu tiên sau nhóm release.
      // Sau đó một tiến trình có thể chạy hoặc exit,
      // nên không tiếp tục coi cả hai vẫn đang chờ.
      have_a = 0;
      have_b = 0;
    }

    if (rt_trace_read(count, &e) != 0)
      trace_ok = 0;
  }

  printf("ORDER TRACE: events=%d lost=%d comparisons=%d\n", count, lost,
         comparisons);

  // Lỗi thực sự: không thử lại để che lỗi.
  if (!workers_ok || !trace_ok || !order_ok)
    return report(name, 0);

  // -1 chỉ có nghĩa chưa tạo được tình huống cần kiểm tra.
  if (comparisons == 0) {
    printf("ORDER SETUP: missing simultaneous-release scenario\n");
    return -1;
  }

  return report(name, 1);
}

static int
test_edf_order(int period_a, int period_b, int equal_deadline)
{
  for (int attempt = 1; attempt <= 3; attempt++) {
    int result = test_edf_order_once(period_a, period_b, equal_deadline);

    // Đã có kết quả PASS hoặc lỗi thực sự thì kết thúc ngay.
    if (result >= 0)
      return result;

    printf("ORDER SETUP: attempt %d/3 had no comparison\n", attempt);
  }

  // Không được coi thiếu tình huống là PASS.
  printf("ORDER SETUP: insufficient coverage after 3 attempts\n");
  return report(
    equal_deadline ? "EDF tie-break by PID" : "EDF earliest deadline", 0);
}

static int
test_fallback(void)
{
  if (rt_trace_ctl(RT_TRACE_START) != 0)
    return report("fallback", 0);

  int parent = getpid();
  int child = spawn_periodic(8);

  if (child < 0) {
    rt_trace_ctl(RT_TRACE_STOP);
    return report("fallback", 0);
  }

  // Cha là tiến trình thường, hoạt động trong lúc con chạy RT.
  uint start = uptime();
  while ((uint)(uptime() - start) < 32)
    pause(1);

  int worker_ok = wait_success();
  int count = rt_trace_ctl(RT_TRACE_STOP);
  int lost = rt_trace_ctl(RT_TRACE_LOST);

  int valid = count > 0 && lost == 0;
  int waiting = 0;
  int observed = 0;
  uint64 next_release = 0;
  struct rt_trace_event e;

  for (int i = 0; i < count; i++) {
    if (rt_trace_read(i, &e) != 1) {
      valid = 0;
      break;
    }

    if (e.seq != i)
      valid = 0;

    if (e.pid == child && e.type == RT_EV_COMPLETE) {
      waiting = 1;
      next_release = e.next_release;
    }

    if (e.pid == child && e.type == RT_EV_RELEASE)
      waiting = 0;

    // Chỉ tính khi cha chạy trên CPU phục vụ RT,
    // sau completion và trước release kế tiếp của con.
    if (e.type == RT_EV_NORMAL && e.pid == parent && e.cpu == 0 && waiting &&
        e.tick < next_release)
      observed++;
  }

  printf("FALLBACK: observations=%d lost=%d\n", observed, lost);
  return report("fallback", worker_ok && valid && observed > 0);
}

static int preempt_delay = -1;

static int
test_preemption(void)
{
  preempt_delay = -1;
  if (rt_trace_ctl(RT_TRACE_START) != 0)
    return report("preemption", 0);

  // Tác vụ deadline gần: hoàn thành nhanh rồi chờ kỳ mới.
  int high = spawn_periodic(8);

  // Tác vụ deadline xa: chạy đủ lâu để bị tác vụ trên ngắt.
  int low = fork();

  if (low == 0) {
    if (sched_setdeadline(80, 30) != 0)
      exit(1);

    uint start = uptime();

    while ((uint)(uptime() - start) < 26) {
      for (volatile int n = 0; n < 10000000; n++) {
      }
    }

    exit(0);
  }

  int workers_ok = high > 0 && low > 0;
  int children = (high > 0) + (low > 0);

  for (int i = 0; i < children; i++) {
    if (!wait_success())
      workers_ok = 0;
  }

  int count = rt_trace_ctl(RT_TRACE_STOP);
  int lost = rt_trace_ctl(RT_TRACE_LOST);
  int valid = count > 0 && lost == 0;

  uint64 low_tick = ~0ULL;
  uint64 low_deadline = 0;
  uint64 release_tick = 0;

  int pending = 0;
  int observed = 0;
  int correct = 1;
  struct rt_trace_event e;

  for (int i = 0; i < count; i++) {
    if (rt_trace_read(i, &e) != 1) {
      valid = 0;
      break;
    }

    if (e.seq != i)
      valid = 0;

    // Bằng chứng low đang chạy tại timer và vẫn còn budget.
    if (e.pid == low && e.type == RT_EV_TICK) {
      low_tick = e.remaining > 0 ? e.tick : ~0ULL;
      low_deadline = e.deadline;
    }

    // High được release ngay tại tick đang phục vụ low.
    if (e.pid == high && e.type == RT_EV_RELEASE && e.job_id > 1 &&
        e.kernel_resume == 0 && low_tick == e.tick &&
        e.deadline < low_deadline) {
      pending = 1;
      release_tick = e.tick;
    }

    if (!pending || e.cpu != 0)
      continue;

    // Cho phép hoàn tất đường kernel; kiểm tra lần chọn EDF kế tiếp.
    if (e.type == RT_EV_DISPATCH && e.aux == RT_TRACE_EDF) {
      int ok = e.pid == high;

      int delay = (int)(e.tick - release_tick);
      if (ok && delay > preempt_delay)
        preempt_delay = delay;

      observed++;
      correct &= ok;
      pending = 0;

      printf("PREEMPT: expected=%d actual=%d delay_ticks=%d [%s]\n", high,
             e.pid, (int)(e.tick - release_tick), ok ? "PASS" : "FAIL");
    } else if (e.type == RT_EV_NORMAL) {
      // High đang đủ điều kiện nhưng lại chọn tiến trình thường.
      observed++;
      correct = 0;
      pending = 0;
    }
  }

  if (pending)
    correct = 0;

  printf("PREEMPT TRACE: observations=%d lost=%d\n", observed, lost);

  if (observed == 0)
    printf("PREEMPT: missing required scenario\n");

  return report("preemption", workers_ok && valid && correct && observed > 0);
}

static int
run_benchmark(void)
{
  int sum = 0;
  int smallest = 0;
  int largest = 0;

  printf("BENCH: preemption response, unit=tick, repetitions=5\n");

  for (int i = 0; i < 5; i++) {
    // Mỗi lượt lấy độ trễ lớn nhất trong các tình huống đã quan sát.
    if (!test_preemption() || preempt_delay < 0) {
      printf("BENCH RESULT: FAIL (invalid run %d)\n", i + 1);
      return 0;
    }

    int value = preempt_delay;
    sum += value;

    if (i == 0 || value < smallest)
      smallest = value;

    if (i == 0 || value > largest)
      largest = value;

    printf("BENCH SAMPLE %d: max_delay=%d ticks\n", i + 1, value);
  }

  // In trung bình hai chữ số thập phân bằng phép toán số nguyên.
  int mean100 = sum * 100 / 5;

  printf("BENCH SUMMARY: n=5 mean=%d.%d%d ticks "
         "min=%d max=%d range=%d ticks\n",
         mean100 / 100, (mean100 % 100) / 10, mean100 % 10, smallest, largest,
         largest - smallest);

  printf("BENCH RESULT: PASS\n");
  return 1;
}

int
main(int argc, char *argv[])
{
  if (argc == 2 && strcmp(argv[1], "--bench") == 0)
    exit(run_benchmark() ? 0 : 1);
  int runs = 1;

  if (argc == 2)
    runs = atoi(argv[1]);
  else if (argc != 1) {
    fprintf(2, "usage: rt_edftest [runs]\n");
    exit(1);
  }

  if (runs < 1 || runs > 100) {
    fprintf(2, "runs must be between 1 and 100\n");
    exit(1);
  }

  int passed = 0;

  for (int i = 0; i < runs; i++) {
    printf("\nEDF RUN %d/%d\n", i + 1, runs);

    // Chạy riêng hai ca để cả hai đều được thực hiện.
    int deadline_ok = test_edf_order(8, 12, 0);
    int tie_ok = test_edf_order(8, 8, 1);
    int fallback_ok = test_fallback();
    int preempt_ok = test_preemption();

    int ok = deadline_ok && tie_ok && fallback_ok && preempt_ok;

    if (ok)
      passed++;

    printf("EDF RUN %d: %s\n", i + 1, ok ? "PASS" : "FAIL");
  }

  int all_ok = passed == runs;

  printf("\nEDF RESULT: %d/%d %s\n", passed, runs, all_ok ? "PASS" : "FAIL");

  exit(all_ok ? 0 : 1);
}