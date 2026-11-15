#include "kernel/types.h"
#include "kernel/rttrace.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define MAX_TRACKED 8

// Trạng thái kỳ vọng của từng tiến trình, dựng lại từ trace.
struct tracked_job {
  int pid;
  int seen;
  int complete;
  int miss_seen;

  int budget;
  int period;
  int runtime;

  uint64 job_id;
  uint64 release;
  uint64 deadline;
  uint64 next_release;
};

struct check_results {
  int sequence;
  int release;
  int budget;
  int dispatch;
  int completion;
  int carry;

  int releases;
  int ticks;
  int dispatches;
  int completes;
  int misses;
  int refills;
  int budget_zeros;
};

static struct tracked_job tracked[MAX_TRACKED];
static struct check_results checks;

// Tìm trạng thái của PID; cấp ô mới nếu chưa gặp PID này.
static struct tracked_job *
find_job(int pid)
{
  if (pid <= 0)
    return 0;

  for (int i = 0; i < MAX_TRACKED; i++) {
    if (tracked[i].pid == pid)
      return &tracked[i];
  }

  for (int i = 0; i < MAX_TRACKED; i++) {
    if (tracked[i].pid == 0) {
      tracked[i].pid = pid;
      return &tracked[i];
    }
  }

  return 0;
}

static int
report(const char *name, int ok)
{
  printf("CHECK %s: %s\n", name, ok ? "PASS" : "FAIL");
  return ok;
}

static void
check_event(struct rt_trace_event *e, int index)
{
  if (e->seq != index)
    checks.sequence = 0;

  if (e->type == RT_EV_NORMAL)
    return;

  struct tracked_job *s = find_job(e->pid);

  if (s == 0) {
    checks.sequence = 0;
    return;
  }

  if (e->type == RT_EV_RELEASE) {
    checks.releases++;

    // Với D = T, deadline phải bằng release + period.
    if (e->period <= 0 || e->runtime <= 0 || e->runtime > e->period ||
        e->deadline != e->release + (uint64)e->period ||
        e->next_release != e->deadline || e->remaining != e->runtime ||
        e->tick != e->release || e->cpu != 0)
      checks.release = 0;

    if (s->seen) {
      // Job mới chỉ được tạo sau khi job trước đã hoàn thành.
      // Mốc release phải theo chu kỳ đã lưu.
      if (!s->complete || e->job_id != s->job_id + 1 ||
          e->release != s->next_release)
        checks.release = 0;
    } else {
      if (e->job_id != 1)
        checks.release = 0;
    }

    s->seen = 1;
    s->complete = 0;
    s->miss_seen = 0;

    s->budget = e->remaining;
    s->period = e->period;
    s->runtime = e->runtime;

    s->job_id = e->job_id;
    s->release = e->release;
    s->deadline = e->deadline;
    s->next_release = e->next_release;
    return;
  }

  // Các sự kiện còn lại phải thuộc một job đã được release.
  // Carry-over cũng phải giữ nguyên ID, release và deadline.
  if (!s->seen || e->job_id != s->job_id || e->release != s->release ||
      e->deadline != s->deadline || e->period != s->period ||
      e->runtime != s->runtime) {
    checks.sequence = 0;
    return;
  }

  switch (e->type) {
  case RT_EV_TICK:
    checks.ticks++;

    // Budget phải giảm đúng một đơn vị trên mỗi bản ghi TICK.
    if (s->complete || e->aux != s->budget || e->aux <= 0 ||
        e->remaining != e->aux - 1 || e->cpu != 0)
      checks.budget = 0;

    s->budget = e->remaining;
    break;

  case RT_EV_BUDGET0:
    checks.budget_zeros++;

    if (s->complete || e->remaining != 0 || s->budget != 0)
      checks.budget = 0;
    break;

  case RT_EV_DISPATCH:
    checks.dispatches++;

    // Chính sách của nhóm: tiến trình RT active chạy trên CPU 0.
    if (e->cpu != 0)
      checks.dispatch = 0;

    if (e->aux == RT_TRACE_EDF) {
      // Chỉ dispatch workload khi job đủ điều kiện.
      if (s->complete || e->kernel_resume != 0 || e->remaining <= 0 ||
          e->remaining != s->budget || e->tick < e->release)
        checks.dispatch = 0;
    } else if (e->aux != RT_TRACE_KERNEL) {
      checks.dispatch = 0;
    }
    break;

  case RT_EV_COMPLETE:
    checks.completes++;

    if (s->complete || e->remaining != 0)
      checks.completion = 0;

    s->complete = 1;
    s->budget = 0;
    s->next_release = e->next_release;
    break;

  case RT_EV_MISS:
    checks.misses++;

    // Không đếm lặp miss của cùng một job.
    if (s->complete || s->miss_seen || e->tick < e->deadline)
      checks.carry = 0;

    s->miss_seen = 1;
    break;

  case RT_EV_REFILL:
    checks.refills++;

    // Cấp budget tại mốc chu kỳ, không đổi job đang dang dở.
    if (s->complete || !s->miss_seen || e->tick != s->next_release ||
        e->remaining != s->runtime ||
        e->next_release != s->next_release + (uint64)s->period)
      checks.carry = 0;

    s->budget = e->remaining;
    s->next_release = e->next_release;
    break;

  default:
    checks.sequence = 0;
    break;
  }
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

// Tạo workload cố ý kéo dài để xuất hiện budget hết và deadline miss.
static int
spawn_overrun(void)
{
  int pid = fork();

  if (pid != 0)
    return pid;

  if (sched_setdeadline(6, 1) != 0)
    exit(1);

  uint start = uptime();

  // Giữ job chưa hoàn thành qua nhiều mốc chu kỳ.
  while ((uint)(uptime() - start) < 18) {
    for (volatile int n = 0; n < 10000000; n++) {
    }
  }

  if (rt_job_done() != 0)
    exit(1);

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

static int
run_once(void)
{
  memset(tracked, 0, sizeof(tracked));
  memset(&checks, 0, sizeof(checks));

  checks.sequence = 1;
  checks.release = 1;
  checks.budget = 1;
  checks.dispatch = 1;
  checks.completion = 1;
  checks.carry = 1;

  if (rt_trace_ctl(RT_TRACE_START) != 0) {
    printf("CHECK trace start: FAIL\n");
    return 0;
  }

  int workers_ok = 1;
  int children = 0;

  int pid = spawn_periodic(8);
  if (pid < 0)
    workers_ok = 0;
  else
    children++;

  pid = spawn_periodic(12);
  if (pid < 0)
    workers_ok = 0;
  else
    children++;

  // Thu hồi hết các con trước khi chuyển sang workload quá thời gian.
  for (int i = 0; i < children; i++) {
    if (!wait_success())
      workers_ok = 0;
  }

  pid = spawn_overrun();

  if (pid < 0) {
    workers_ok = 0;
  } else {
    if (!wait_success())
      workers_ok = 0;
  }

  // Dừng trước khi đọc để các bản ghi không thay đổi.
  int count = rt_trace_ctl(RT_TRACE_STOP);
  int lost = rt_trace_ctl(RT_TRACE_LOST);
  int read_ok = count >= 0 && lost >= 0;

  struct rt_trace_event event;

  if (count >= 0) {
    for (int i = 0; i < count; i++) {
      if (rt_trace_read(i, &event) != 1) {
        read_ok = 0;
        break;
      }

      // In dữ liệu sau khi đã dừng trace để tìm bản ghi sai.
      // Ép về int chỉ để đọc các giá trị nhỏ trong lượt test ngắn này.
      printf("TRACE i=%d type=%d cpu=%d pid=%d job=%d tick=%d "
             "r=%d d=%d next=%d C=%d rem=%d kr=%d aux=%d\n",
             i, event.type, event.cpu, event.pid, (int)event.job_id,
             (int)event.tick, (int)event.release, (int)event.deadline,
             (int)event.next_release, event.runtime, event.remaining,
             event.kernel_resume, event.aux);

      check_event(&event, i);
    }

    // Đọc ngay sau bản ghi cuối phải báo hết dữ liệu.
    if (rt_trace_read(count, &event) != 0)
      read_ok = 0;
  }

  printf("TRACE: events=%d lost=%d\n", count, lost);
  printf("EVENTS: release=%d tick=%d dispatch=%d complete=%d\n",
         checks.releases, checks.ticks, checks.dispatches, checks.completes);
  printf("EVENTS: budget0=%d miss=%d refill=%d\n", checks.budget_zeros,
         checks.misses, checks.refills);

  int ok = 1;

  ok &= report("workers", workers_ok);

  ok &= report("trace integrity",
               read_ok && lost == 0 && checks.sequence && count > 0);

  // Mỗi tiến trình định kỳ có bốn release:
  // release đầu và ba release sau ba lần rt_job_done.
  // Workload quá thời gian có hai release.
  // Tổng cộng phải có 4 + 4 + 2 = 10.
  ok &= report("release", checks.release && checks.releases == 10);

  ok &= report("budget transitions",
               checks.budget && checks.ticks > 0 && checks.budget_zeros > 0);

  ok &= report("dispatch eligibility and CPU",
               checks.dispatch && checks.dispatches > 0);

  // Hai tiến trình đầu hoàn thành ba lần mỗi tiến trình.
  // Tiến trình quá thời gian hoàn thành một lần.
  ok &= report("completion", checks.completion && checks.completes == 7);

  ok &= report("deadline miss and carry-over",
               checks.carry && checks.misses > 0 && checks.refills > 0);

  return ok;
}

// Đếm số trang RAM có thể cấp phát, rồi trả lại toàn bộ.
// Dùng sbrk cấp phát ngay của bản xv6 hiện tại.
static int
countfree(void)
{
  int n = 0;
  uint64 sz0 = (uint64)sbrk(0);

  while (sbrk(PGSIZE) != SBRK_ERROR)
    n++;

  // Kiểm tra việc trả lại bộ nhớ vừa xin.
  int allocated = (int)((uint64)sbrk(0) - sz0);

  if (allocated > 0 && sbrk(-allocated) == SBRK_ERROR)
    return -1;

  return n;
}

int
main(int argc, char *argv[])
{
  int runs = 1;

  if (argc == 2)
    runs = atoi(argv[1]);
  else if (argc != 1) {
    fprintf(2, "usage: rt_w4test [runs]\n");
    exit(1);
  }

  if (runs < 1 || runs > 100) {
    fprintf(2, "runs must be between 1 and 100\n");
    exit(1);
  }

  // Đo RAM trước toàn bộ các lượt kiểm thử.
  int pages_before = countfree();

  if (pages_before <= 0) {
    printf("FREE PAGES: FAIL (initial measurement failed)\n");
    printf("W4 SUITE: FAIL\n");
    exit(1);
  }

  int passed = 0;

  for (int i = 0; i < runs; i++) {
    printf("\nW4 RUN %d/%d\n", i + 1, runs);

    // Kiểm tra vòng đời job, budget, completion và carry-over.
    int ok = run_once();

    if (ok)
      passed++;

    printf("W4 RUN %d: %s\n", i + 1, ok ? "PASS" : "FAIL");
  }

  int pages_after = countfree();
  int pages_ok = pages_after >= pages_before;
  int all_ok = passed == runs && pages_ok;

  printf("\nW4 RESULT: %d/%d %s\n", passed, runs,
         passed == runs ? "PASS" : "FAIL");

  printf("FREE PAGES: before=%d after=%d [%s]\n", pages_before, pages_after,
         pages_ok ? "PASS" : "FAIL");

  printf("W4 SUITE: %s\n", all_ok ? "PASS" : "FAIL");

  exit(all_ok ? 0 : 1);
}