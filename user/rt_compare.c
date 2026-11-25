#include "kernel/types.h"
#include "kernel/schedpolicy.h"
#include "kernel/rttrace.h"
#include "user/user.h"

#define NTASK        3
#define NJOB         3
#define TIME_SCALE   10
#define WORK_PERCENT 90

// Hai hàm được định nghĩa trong rt_calibrate.c.
void run_blocks(int blocks);
int calibrate(int *measured_blocks, int *maximum_ticks);

struct task {
  int c;
  int t;
};

// Thứ tự luôn là A, B, C; mỗi cặp lưu C trước, T sau.
static struct task safe_set[NTASK] = {{1 * TIME_SCALE, 4 * TIME_SCALE},
                                      {2 * TIME_SCALE, 5 * TIME_SCALE},
                                      {1 * TIME_SCALE, 10 * TIME_SCALE}};
static struct task compare_set[NTASK] = {{1 * TIME_SCALE, 3 * TIME_SCALE},
                                         {1 * TIME_SCALE, 4 * TIME_SCALE},
                                         {2 * TIME_SCALE, 7 * TIME_SCALE}};
static int pids[NTASK];

// FNV-1a trên từng byte số nguyên, không phụ thuộc padding của struct.
static uint
hash_number(uint hash, uint value)
{
  for (int i = 0; i < 4; i++) {
    hash = (hash ^ (value & 255)) * 16777619U;
    value >>= 8;
  }
  return hash;
}

static uint
input_hash(struct task *set, int *blocks)
{
  uint hash = hash_number(2166136261U, NJOB);
  for (int i = 0; i < NTASK; i++) {
    hash = hash_number(hash, set[i].c);
    hash = hash_number(hash, set[i].t);
    hash = hash_number(hash, blocks[i]);
  }
  // Không đưa policy vào hash; hai policy dùng cùng đầu vào.
  return hash;
}

// Đăng ký trước, báo sẵn sàng rồi chờ cha mở cổng.
static void
worker(struct task task, int blocks, int gate, int ready_fd)
{
  int rc = sched_setdeadline(task.t, task.c);
  char result = rc == 0 ? 'R' : 'F';
  int sent = write(ready_fd, &result, 1);
  close(ready_fd);
  if (rc != 0 || sent != 1) {
    close(gate);
    exit(2);
  }
  char token;
  int received = read(gate, &token, 1);
  close(gate);
  if (received != 1)
    exit(1);
  for (int job = 0; job < NJOB; job++) {
    run_blocks(blocks);
    // Báo hoàn thành rồi chờ kỳ tiếp theo trước khi trở lại user.
    if (rt_job_done() != 0)
      exit(3);
  }
  exit(0);
}

static int
task_index(int pid)
{
  for (int i = 0; i < NTASK; i++)
    if (pids[i] == pid)
      return i;
  return -1;
}

// Chỉ xuất sự kiện; Thành phân tích vòng đời job và thứ tự EDF/RM từ CSV.
static int
dump_trace(int count, int *misses)
{
  printf(
    "CSV,seq,type,cpu,pid,job,tick,release,deadline,next_release,T,C,remaining,kr,aux,misses\n");
  *misses = 0;
  for (int i = 0; i < count; i++) {
    struct rt_trace_event e;
    if (rt_trace_read(i, &e) != 1)
      return 0;
    printf("CSV,%d,%d,%d,%d,%lu,%lu,%lu,%lu,%lu,%d,%d,%d,%d,%d,%lu\n", e.seq,
           e.type, e.cpu, e.pid, e.job_id, e.tick, e.release, e.deadline,
           e.next_release, e.period, e.runtime, e.remaining, e.kernel_resume,
           e.aux, e.misses);
    // Chỉ đếm miss của các worker trong lần chạy hiện tại.
    if (task_index(e.pid) >= 0 && e.type == RT_EV_MISS)
      (*misses)++;
  }
  return 1;
}

static int
run_workload(int policy, const char *name, struct task *set, int *blocks)
{
  if (sched_setpolicy(policy) != 0) {
    printf("WORKLOAD RESULT: FAIL (set policy)\n");
    return 0;
  }
  printf("\nSOURCE=XV6 policy=%s set=%s jobs=%d time_scale=%d input_hash=%x\n",
         policy == SCHED_EDF ? "edf" : "rm", name, NJOB, TIME_SCALE,
         input_hash(set, blocks));
  for (int i = 0; i < NTASK; i++) {
    printf("INPUT %c: C=%d T=%d blocks=%d\n", 'A' + i, set[i].c, set[i].t,
           blocks[i]);
    pids[i] = -1;
  }
  int gate[2], ready_pipe[2];
  if (pipe(gate) < 0) {
    printf("WORKLOAD RESULT: FAIL (gate pipe)\n");
    return 0;
  }
  if (pipe(ready_pipe) < 0) {
    close(gate[0]);
    close(gate[1]);
    printf("WORKLOAD RESULT: FAIL (ready pipe)\n");
    return 0;
  }
  // Bắt đầu trước fork để không bỏ mất RELEASE của con.
  if (rt_trace_ctl(RT_TRACE_START) != 0) {
    close(gate[0]);
    close(gate[1]);
    close(ready_pipe[0]);
    close(ready_pipe[1]);
    printf("WORKLOAD RESULT: FAIL (trace start)\n");
    return 0;
  }
  int spawned = 0, ok = 1;
  for (int i = 0; i < NTASK; i++) {
    int pid = fork();
    if (pid < 0) {
      ok = 0;
      break;
    }
    if (pid == 0) {
      close(gate[1]);
      close(ready_pipe[0]);
      worker(set[i], blocks[i], gate[0], ready_pipe[1]);
      exit(1);
    }
    pids[spawned++] = pid;
  }
  close(gate[0]);
  close(ready_pipe[1]);
  if (ok) {
    for (int i = 0; i < NTASK; i++) {
      char result;
      if (read(ready_pipe[0], &result, 1) != 1 || result != 'R') {
        ok = 0;
        break;
      }
    }
  }
  close(ready_pipe[0]);
  if (ok && write(gate[1], "ABC", NTASK) != NTASK)
    ok = 0;
  // EOF giúp con đang chờ thoát nếu chuẩn bị thất bại.
  close(gate[1]);
  int seen[NTASK] = {0};
  for (int i = 0; i < spawned; i++) {
    int status = -1;
    int pid = wait(&status);
    int k = task_index(pid);
    if (k < 0) {
      ok = 0;
      continue;
    }
    if (seen[k] || status != 0)
      ok = 0;
    seen[k] = 1;
  }
  for (int i = 0; i < spawned; i++) {
    if (!seen[i])
      ok = 0;
  }
  printf("CHECK workers: %s\n", ok ? "PASS" : "FAIL");
  int count = rt_trace_ctl(RT_TRACE_STOP);
  int lost = rt_trace_ctl(RT_TRACE_LOST);
  printf("TRACE: events=%d lost=%d\n", count, lost);
  // Mất sự kiện hoặc đọc không đủ CSV là lỗi xuất dữ liệu.
  int misses = 0;
  int exported = count > 0 && lost == 0 && dump_trace(count, &misses);
  printf("CHECK trace export: %s\n", exported ? "PASS" : "FAIL");
  for (int i = 0; i < NTASK; i++)
    printf("TASK %c: pid=%d\n", 'A' + i, pids[i]);
  // CSV đầy đủ mới cho phép kết luận không có miss.
  printf("OBSERVED MISSES: %d\n", misses);
  int deadlines_ok = exported && misses == 0;
  printf("CHECK no deadline miss: %s\n", deadlines_ok ? "PASS" : "FAIL");
  ok &= exported && deadlines_ok;
  printf("WORKLOAD RESULT: %s\n", ok ? "PASS" : "FAIL");
  return ok;
}

int
main(int argc, char *argv[])
{
  if (argc != 2 ||
      (strcmp(argv[1], "safe") != 0 && strcmp(argv[1], "compare") != 0)) {
    fprintf(2, "usage: rt_compare safe|compare\n");
    exit(1);
  }
  int original = sched_getpolicy();
  if (original != SCHED_EDF && original != SCHED_RM) {
    printf("PAIRED WORKLOAD RESULT: FAIL (get policy)\n");
    exit(1);
  }
  struct task *set = strcmp(argv[1], "safe") == 0 ? safe_set : compare_set;
  // Đo trên cả hai policy trước khi chọn workload chung cho cặp.
  int measured_blocks = 0, maximum_ticks = 0;
  int calibration_ok = 1;
  for (int policy = SCHED_EDF; policy <= SCHED_RM; policy++) {
    int sample_blocks = 0, sample_ticks = 0;
    printf("CAL POLICY: %s\n", policy == SCHED_EDF ? "edf" : "rm");
    if (sched_setpolicy(policy) != 0 ||
        calibrate(&sample_blocks, &sample_ticks) < 0 || sample_blocks <= 0 ||
        sample_ticks <= 0) {
      calibration_ok = 0;
      break;
    }
    // So sánh tick/block bằng nhân chéo, có tính một tick dự phòng.
    if (measured_blocks == 0 ||
        ((uint64)sample_ticks + 1) * (uint64)measured_blocks >
          ((uint64)maximum_ticks + 1) * (uint64)sample_blocks) {
      measured_blocks = sample_blocks;
      maximum_ticks = sample_ticks;
    }
  }
  if (!calibration_ok) {
    int restored = sched_setpolicy(original) == 0;
    printf("CHECK restore policy: %s\n", restored ? "PASS" : "FAIL");
    printf("PAIRED WORKLOAD RESULT: FAIL (calibration)\n");
    exit(1);
  }
  printf("CAL SELECTED: blocks=%d ticks=%d\n", measured_blocks, maximum_ticks);
  int blocks[NTASK];
  for (int i = 0; i < NTASK; i++) {
    // Ước lượng workload theo budget, dự phòng một tick và dùng mức 90%.
    uint64 numerator =
      (uint64)measured_blocks * (uint64)set[i].c * WORK_PERCENT;
    uint64 denominator = ((uint64)maximum_ticks + 1) * 100;
    uint64 amount = numerator / denominator;
    if (amount < 1 || amount > 100000000) {
      printf("PAIRED WORKLOAD RESULT: FAIL (workload range)\n");
      exit(1);
    }
    blocks[i] = (int)amount;
  }
  printf("WORKLOAD: calibrated fixed blocks; "
         "elapsed-time estimate, not a WCET guarantee\n");
  // Cả hai lượt dùng cùng mảng blocks; không hiệu chỉnh giữa hai lượt.
  int edf_ok = run_workload(SCHED_EDF, argv[1], set, blocks);
  int rm_ok = run_workload(SCHED_RM, argv[1], set, blocks);
  int restored = sched_setpolicy(original) == 0;
  int all_ok = edf_ok && rm_ok && restored;
  printf("\nCHECK restore policy: %s\n", restored ? "PASS" : "FAIL");
  printf("PAIRED WORKLOAD RESULT: %s\n", all_ok ? "PASS" : "FAIL");
  printf("TRACE VALIDATION: requires Thanh's checker\n");
  printf("ORDER COMPARISON: requires separate trace validation\n");
  exit(all_ok ? 0 : 1);
}
