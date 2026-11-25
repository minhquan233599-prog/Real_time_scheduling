# Tuần 5 — Code và kiểm tra phần Dũng

## 1. Dũng làm phần nào?

Dũng chuẩn bị một workload dùng chung để chạy các bộ tác vụ chuẩn dưới EDF và RM, kiểm tra chương trình con hoàn thành, ghi trace và bàn giao dữ liệu cho Thành. Đồng thời phụ trách kiểm thử giao diện đổi policy do Quân triển khai.

Tài liệu đối chiếu với `kernel/rt.c`, `kernel/rttrace.c`, header trace và printf trên main ngày 18/11/2026 của https://github.com/minhquan233599-prog/Real_time_scheduling. Bộ tác vụ lấy từ trang 10, yêu cầu Sprint 5 ở trang 30 của tài liệu “Real-time Scheduling EDF/RM — Giáo trình và 8 Sprint”. Chỉ đọc GitHub, không chỉnh sửa repository trực tuyến.

| File | Dũng cần làm |
|---|---|
| `user/rt_compare.c` | Tạo mới, dùng toàn bộ code ở phần 4 |
| `user/rt_policytest.c` | Dùng đúng bản đã cung cấp trong hướng dẫn Quân, không viết thêm bản thứ hai |
| `Makefile` | Thêm chương trình để tự chạy test; phối hợp với Thành khi tích hợp |
| `kernel/rt.c` | Rà soát vòng đời; hiện chưa cần sửa chỉ vì thêm RM |

Điều kiện trước khi bắt đầu: đã tích hợp phần Quân, gồm `sched_setpolicy()`, `sched_getpolicy()`, header `schedpolicy.h` và `RT_TRACE_RM = 2`. Nếu thiếu thì hoàn tất phần Quân trước, không tự đổi tên API hoặc số syscall.

## 2. Hai bộ tác vụ dùng chung

Mỗi tác vụ thực hiện 3 job. Thứ tự trong bảng là **C, T**, còn syscall nhận **T, C**.

| Bộ | A (C,T) | B (C,T) | C (C,T) | Tổng tải |
|---|---|---|---|---|
| SAFE | (1,4) | (2,5) | (1,10) | 0,75 |
| COMPARE | (1,3) | (1,4) | (2,7) | 73/84 ≈ 0,8690 |

Trong mô hình lý tưởng của COMPARE: tại t=4, EDF chọn C1 vì deadline 7 sớm hơn deadline 8 của B2; RM chọn B2 vì T=4 ngắn hơn T=7. Đây là mốc đối chiếu, không được đổi oracle nếu kết quả kernel khác.

Chương trình bên dưới giữ nguyên C/T, số job và lượng công việc khi đổi policy. Input nằm trong hai bảng cố định trong cùng một chương trình; hiện chưa đọc task set từ file ngoài. `input_hash` là FNV-1a 32 bit của cấu hình, dùng nhận diện thực nghiệm, không phải hash bảo mật. Đối chiếu thêm các dòng INPUT, work và cùng commit khi so sánh.

## 3. Ba giới hạn cần hiểu trước khi chạy

1. **C là budget đăng ký, không tự tạo công việc dài đúng C tick.** Code dùng số phép tính cố định, nhân theo C. Tham số `work` điều khiển lượng tính toán; phải đo thực tế trên QEMU. Không dùng `uptime()` để kết luận thời gian CPU vì nó còn bao gồm thời gian chờ.
2. **Pipe chỉ giữ các con đến khi cha tạo xong và bắt đầu trace.** Ba con vẫn đăng ký lần lượt, nên không bảo đảm release chung một tick. Dòng `first_release` giúp phát hiện độ lệch. Không gọi đây là barrier đồng bộ release tuyệt đối.
3. **WORKLOAD PASS là chương trình chạy đúng các kiểm tra đã nêu.** Nó chưa chứng minh quyết định t=4 hoặc mọi lần chọn RM đúng. Checker thứ tự/preemption của Thành và dữ liệu đối chiếu mới hoàn tất phần đó.

Nếu các job quá ngắn, chúng có thể COMPLETE mà không có TICK nào. Khi đó workload có thể PASS nhưng chưa đủ minh chứng so sánh lịch. Nếu workload quá dài và xuất hiện miss, lượt đó phải FAIL. Không xóa kiểm tra miss để làm kết quả xanh.

## 4. Tạo user/rt_compare.c

Copy toàn bộ khối dưới đây vào file mới và lưu lại:

```c
#include "kernel/types.h"
#include "kernel/schedpolicy.h"
#include "kernel/rttrace.h"
#include "user/user.h"

#define NTASK 3
#define NJOB 3

struct task {
  int c;
  int t;
};

// Thứ tự luôn là A, B, C; mỗi cặp lưu C trước, T sau.
static struct task safe_set[NTASK] = {{1, 4}, {2, 5}, {1, 10}};
static struct task compare_set[NTASK] = {{1, 3}, {1, 4}, {2, 7}};
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
input_hash(struct task *set, int work)
{
  uint hash = 2166136261U;
  hash = hash_number(hash, NJOB);
  hash = hash_number(hash, work);
  for (int i = 0; i < NTASK; i++) {
    hash = hash_number(hash, set[i].c);
    hash = hash_number(hash, set[i].t);
  }
  // Không đưa policy vào hash để đối chiếu đầu vào EDF và RM.
  return hash;
}

static void
worker(struct task task, int gate, int work)
{
  char token;
  int ready = read(gate, &token, 1);
  close(gate);
  if (ready != 1)
    exit(1);

  // Syscall nhận T trước, C sau; khác thứ tự trong bảng task.
  if (sched_setdeadline(task.t, task.c) != 0)
    exit(2);

  volatile uint value = 1;
  for (int job = 0; job < NJOB; job++) {
    // Khối lượng công việc cố định, giống nhau ở EDF và RM.
    // work không phải số tick; thời gian thực cần quan sát từ trace.
    for (int block = 0; block < task.c * work; block++) {
      for (int n = 0; n < 10000; n++)
        value = value * 1664525U + 1013904223U;
    }
    if (rt_job_done() != 0)
      exit(3);
  }
  // Không printf khi trace đang thu, tránh gây nhiễu workload.
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

static int
dump_trace(int count, int policy)
{
  int releases[NTASK] = {0};
  int completes[NTASK] = {0};
  int samples[NTASK] = {0};
  int dispatches[NTASK] = {0};
  uint64 first_release[NTASK] = {0};
  int misses = 0;
  int ok = count > 0;
  int expected = policy == SCHED_EDF ? RT_TRACE_EDF : RT_TRACE_RM;

  printf("CSV,seq,type,cpu,pid,job,tick,release,deadline,next_release,T,C,remaining,kr,aux,misses\n");
  for (int i = 0; i < count; i++) {
    struct rt_trace_event e;
    if (rt_trace_read(i, &e) != 1) {
      ok = 0;
      break;
    }
    if (e.seq != i)
      ok = 0;
    printf("CSV,%d,%d,%d,%d,%lu,%lu,%lu,%lu,%lu,%d,%d,%d,%d,%d,%lu\n",
           e.seq, e.type, e.cpu, e.pid, e.job_id, e.tick,
           e.release, e.deadline, e.next_release, e.period, e.runtime,
           e.remaining, e.kernel_resume, e.aux, e.misses);

    int k = task_index(e.pid);
    if (k < 0 || e.type == RT_EV_NORMAL)
      continue;
    if (e.cpu != 0)
      ok = 0;
    if (e.type == RT_EV_RELEASE) {
      if (releases[k] == 0)
        first_release[k] = e.release;
      releases[k]++;
    } else if (e.type == RT_EV_COMPLETE) {
      completes[k]++;
    } else if (e.type == RT_EV_TICK) {
      samples[k]++;
    } else if (e.type == RT_EV_MISS) {
      misses++;
    } else if (e.type == RT_EV_DISPATCH) {
      if (e.aux == expected)
        dispatches[k]++;
      else if (e.aux != RT_TRACE_KERNEL)
        ok = 0;
    }
  }

  for (int i = 0; i < NTASK; i++) {
    // job_done chờ sang kỳ mới nên có thể thấy thêm RELEASE sau job cuối.
    if (releases[i] < NJOB || completes[i] != NJOB || dispatches[i] == 0)
      ok = 0;
    printf("TASK %c: pid=%d first_release=%lu releases=%d complete=%d ticks=%d dispatch=%d\n",
           'A' + i, pids[i], first_release[i], releases[i],
           completes[i], samples[i], dispatches[i]);
  }
  printf("OBSERVED MISSES: %d\n", misses);
  printf("CHECK no deadline miss: %s\n", misses == 0 ? "PASS" : "FAIL");
  printf("CHECK trace and completion: %s\n", ok ? "PASS" : "FAIL");
  return ok && misses == 0;
}

int
main(int argc, char *argv[])
{
  if (argc != 4 ||
      (strcmp(argv[1], "edf") != 0 && strcmp(argv[1], "rm") != 0) ||
      (strcmp(argv[2], "safe") != 0 && strcmp(argv[2], "compare") != 0)) {
    fprintf(2, "usage: rt_compare edf|rm safe|compare work\n");
    exit(1);
  }
  int work = atoi(argv[3]);
  if (work < 1 || work > 1000) {
    fprintf(2, "work must be between 1 and 1000\n");
    exit(1);
  }
  int policy = strcmp(argv[1], "edf") == 0 ? SCHED_EDF : SCHED_RM;
  struct task *set = strcmp(argv[2], "safe") == 0 ? safe_set : compare_set;
  int original = sched_getpolicy();
  if (original != SCHED_EDF && original != SCHED_RM) {
    printf("WORKLOAD RESULT: FAIL (get policy)\n");
    exit(1);
  }
  if (sched_setpolicy(policy) != 0) {
    printf("WORKLOAD RESULT: FAIL (set policy)\n");
    exit(1);
  }

  printf("SOURCE=XV6 policy=%s set=%s jobs=%d work=%d input_hash=%x\n",
         argv[1], argv[2], NJOB, work, input_hash(set, work));
  for (int i = 0; i < NTASK; i++)
    printf("INPUT %c: C=%d T=%d\n", 'A' + i, set[i].c, set[i].t);

  int gate[2];
  if (pipe(gate) < 0) {
    sched_setpolicy(original);
    printf("WORKLOAD RESULT: FAIL (pipe)\n");
    exit(1);
  }

  int spawned = 0;
  int ok = 1;
  for (int i = 0; i < NTASK; i++) {
    int pid = fork();
    if (pid < 0) {
      ok = 0;
      break;
    }
    if (pid == 0) {
      close(gate[1]);
      worker(set[i], gate[0], work);
      exit(1);
    }
    pids[spawned++] = pid;
  }
  close(gate[0]);

  // Chỉ thu trace khi tạo đủ con; không làm gián đoạn phiên trace của người khác.
  int started = ok && rt_trace_ctl(RT_TRACE_START) == 0;
  if (!started)
    ok = 0;
  if (ok && write(gate[1], "ABC", NTASK) != NTASK)
    ok = 0;
  // EOF làm con chưa nhận token thoát; con đã nhận token sẽ hoàn tất bình thường.
  close(gate[1]);

  int seen[NTASK] = {0};
  for (int i = 0; i < spawned; i++) {
    int status = -1;
    int pid = wait(&status);
    int k = task_index(pid);
    if (k < 0 || seen[k] || status != 0)
      ok = 0;
    if (k >= 0)
      seen[k] = 1;
  }

  if (started) {
    int count = rt_trace_ctl(RT_TRACE_STOP);
    int lost = rt_trace_ctl(RT_TRACE_LOST);
    printf("TRACE: events=%d lost=%d\n", count, lost);
    if (count < 0 || lost != 0)
      ok = 0;
    int trace_ok = dump_trace(count, policy);
    ok &= trace_ok;
  }

  // Mọi con đã được wait trước khi đổi lại chính sách.
  int restored = sched_setpolicy(original) == 0;
  ok &= restored;
  printf("CHECK restore policy: %s\n", restored ? "PASS" : "FAIL");
  printf("WORKLOAD RESULT: %s\n", ok ? "PASS" : "FAIL");
  // Đây là kiểm tra workload; oracle quyết định EDF/RM do Thành kiểm tra riêng.
  exit(ok ? 0 : 1);
}

```

## 5. Giải thích các phần code

| Phần | Vai trò |
|---|---|
| `safe_set`, `compare_set` | Lưu đúng hai bộ C/T; không đổi bảng theo policy |
| `hash_number`, `input_hash` | Tạo dấu nhận diện đầu vào; policy được loại ra để hai lượt có thể so sánh |
| `worker` | Chờ token, đăng ký RT, tính toán một lượng cố định, báo hoàn thành 3 lần rồi thoát |
| Pipe `gate` | Cho cha tạo đủ con và bật trace trước khi thả workload |
| `task_index` | Ánh xạ PID thực tế về A/B/C vì PID hai lượt thường khác nhau |
| `dump_trace` | In dữ liệu sau STOP; kiểm tra seq, CPU, lớp dispatch, số COMPLETE và deadline miss |
| `wait` và `seen` | Kiểm tra từng con thoát thành công và không đếm một con hai lần |
| Khôi phục policy | Chỉ thực hiện sau khi đã thu hồi các con, tránh ảnh hưởng test tiếp theo |

Không in từ worker trong thời gian đo. Mọi dòng CSV được in sau khi dừng trace để giảm nhiễu do console.

Mỗi dòng dữ liệu bắt đầu bằng `CSV,`. Thành có thể lọc tiền tố này rồi bỏ bốn ký tự đầu để lấy CSV chuẩn. Các trường RELEASE, DISPATCH, TICK, COMPLETE... giữ mã event tuần 4; `aux` của DISPATCH dùng EDF=1, RM=2, KERNEL=0.

`rt_job_done()` chỉ trả về sau khi được phép chạy kỳ tiếp theo. Vì vậy 3 lần COMPLETE có thể đi cùng 4 lần RELEASE; code yêu cầu đúng 3 COMPLETE và ít nhất 3 RELEASE. Không sửa kernel chỉ để ép số RELEASE bằng số COMPLETE.

## 6. Sửa Makefile và dùng lại policytest

Trong danh sách `UPROGS`, thêm:

```make
    $U/_rt_compare\
```

Nếu `rt_policytest` chưa có, thêm đúng một dòng nữa:

```make
    $U/_rt_policytest\
```

Không thêm các chương trình user vào danh sách object kernel. Tránh thêm hai lần cùng tên. Dòng cuối danh sách có thể bỏ dấu nối `\`.

File `rt_policytest.c` đã có đầy đủ trong [hướng dẫn Quân tuần 5](https://chatgpt.com/library?file_id=libfile_682e76b72b108191bc7045b1d3d08ffe&account_id=personal). Dũng nhận quyền phụ trách file này; Quân có thể chạy để tự kiểm tra phần syscall. Không cần tạo `rt_policytest_dung.c` hay sửa các syscall đã thống nhất.

Bản policytest hiện kiểm tra: đọc/đổi EDF–RM, policy sai, gọi lại cùng policy, từ chối đổi khi một con đang đăng ký RT và đổi được sau khi con thoát. Khi nghiệm thu toàn nhóm vẫn cần kiểm tra race và RM picker riêng.

## 7. Kiểm tra trước khi thu log chính thức

### Trên Ubuntu, tại thư mục gốc dự án

```bash
clang-format -i user/rt_compare.c user/rt_policytest.c
clang-format --dry-run --Werror user/rt_compare.c user/rt_policytest.c
make kernel/kernel fs.img
make qemu CPUS=1
```

Nếu build lỗi thiếu `sched_setpolicy` hoặc `RT_TRACE_RM`, kiểm tra phần Quân đã đủ chưa. Không thêm khai báo giả để lách lỗi liên kết.

### Trong xv6 — thử nhanh

```text
rt_policytest
rt_compare edf safe 100
rt_compare rm safe 100
rt_compare edf compare 100
rt_compare rm compare 100
```

`100` là điểm thử ban đầu, không phải một giá trị đã hiệu chuẩn. Quan sát các dòng TASK:

- `ticks=0` ở nhiều tác vụ: công việc quá ngắn để thể hiện lịch ở độ phân giải hiện tại; có thể thử tăng work.
- Có miss hoặc carry-over kéo dài: xem lượng công việc và release thực tế; có thể giảm work trong lượt thử.
- `lost>0`: dữ liệu trace không đầy đủ, không dùng lượt đó làm minh chứng hoàn chỉnh.
- Khi chọn work khác, chạy lại **cả EDF và RM với cùng giá trị**, không chỉ sửa bên đang FAIL.
- Nếu không tìm được mức workload thể hiện đúng kịch bản, ghi nhận thiếu coverage và kiểm tra nguyên nhân; không kết luận scheduler sai chỉ vì chưa xuất hiện tình huống cần quan sát.

Để minh chứng đúng mốc t=4 theo tài liệu, cần kiểm tra release ban đầu và lượng thực thi phù hợp. Phiên bản API hiện tại release ngay khi đăng ký, nên chương trình này không tự bảo đảm mốc đó. Không đổi deadline, T/C hoặc kết quả mong đợi để làm khớp hình lý thuyết.

## 8. Lưu minh chứng phần Dũng

Chỉ cần một log build, một file phiên bản và hai log chạy CPU1/CPU4. Trong log chạy có thể chứa nhiều lệnh, không cần tách mỗi test ra một file.

### 8.1. Ghi phiên bản và build — Ubuntu

```bash
mkdir -p docs/week5/logs

git rev-parse HEAD > docs/week5/logs/dung-version.txt
git status --short >> docs/week5/logs/dung-version.txt
qemu-system-riscv64 --version >> docs/week5/logs/dung-version.txt
riscv64-unknown-elf-gcc --version >> docs/week5/logs/dung-version.txt
grep -n 'w_stimecmp' kernel/trap.c >> docs/week5/logs/dung-version.txt

set -o pipefail
make kernel/kernel fs.img 2>&1 | tee docs/week5/logs/dung-build.log
build_rc=${PIPESTATUS[0]}
echo "BUILD_EXIT_CODE=$build_rc" | tee -a docs/week5/logs/dung-build.log
```

Chỉ tiếp tục nếu build trả 0. Timer hiện giữ baseline `r_time()+1000000`; với timebase 10 MHz của cấu hình QEMU virt đang dùng, tương ứng khoảng 0,1 giây/tick. Ghi cấu hình thực tế nếu nhóm thay đổi timer.

### 8.2. CPUS=1 — Ubuntu

```bash
script -q -e -c 'make qemu CPUS=1' docs/week5/logs/dung-qemu-cpus1.log
```

Trong xv6, chạy lần lượt (thay 100 đồng nhất bằng work đã chọn ở bước thử nếu cần):

```text
rt_policytest 10
rt_job_smoke
rttest
rt_compare edf safe 100
rt_compare rm safe 100
rt_compare edf compare 100
rt_compare rm compare 100
```

Sau đó thoát bằng Ctrl+A, nhả ra rồi X. Policytest và workload đều khôi phục policy ban đầu; trong phiên khởi động mới policy ban đầu là EDF.

### 8.3. CPUS=4 — Ubuntu

```bash
script -q -e -c 'make qemu CPUS=4' docs/week5/logs/dung-qemu-cpus4.log
```

Trong xv6 chạy lại cùng các lệnh trên, cùng work. Không chạy hai QEMU cùng mở `fs.img`.

Lọc tổng kết trên Ubuntu:

```bash
grep -nE 'SOURCE=|POLICY RESULT|RT JOB SMOKE|WORKLOAD RESULT|OBSERVED MISSES|FAIL|panic' \
  docs/week5/logs/dung-qemu-cpus1.log \
  docs/week5/logs/dung-qemu-cpus4.log
```

Các dòng mong đợi: policy 10/10 PASS; smoke PASS; rttest 6/6 PASS; mỗi workload có lost=0, no deadline miss PASS, trace/completion PASS và WORKLOAD RESULT PASS. Đây là mong đợi, không phải kết quả đã xác nhận.

`script -e` chỉ lấy mã thoát của lệnh chạy QEMU/make; không tự đọc PASS/FAIL của chương trình bên trong. Nếu treo, mất kết nối hoặc chưa có dòng kết thúc thì lượt đó chưa đạt. Bản workload gọn này chưa có watchdog tự dừng; harness nghiệm thu của Thành cần coi timeout là FAIL.

## 9. Dũng bàn giao gì?

- `user/rt_compare.c` và `user/rt_policytest.c` dùng thống nhất với Quân.
- Hai bộ C/T đúng tài liệu, giá trị work đã thử và input_hash tương ứng.
- Log build, phiên bản, CPU1 và CPU4; không tự điền PASS cho lượt chưa chạy.
- Các dòng TASK ánh xạ PID → A/B/C, first_release và số tick quan sát được.
- Trace CSV để Thành kiểm tra quyết định EDF/RM và vẽ biểu đồ; cần gắn policy, CPUS, commit và input_hash vào dữ liệu xuất.

WORKLOAD PASS xác nhận các con kết thúc thành công, đủ COMPLETE, có dispatch đúng lớp policy trên CPU 0, không mất trace và không quan sát miss trong lượt đó. Nó không thay thế kiểm tra thứ tự RM, preemption, cấp/trừ budget chi tiết, tài nguyên hay benchmark 5 mẫu.

Thành tiếp tục nghiệm thu stress ít nhất 10 lượt mỗi CPUS, tài nguyên, hồi quy usertests/grind, benchmark và oracle theo phạm vi chung. Không cần Dũng nhân bản tất cả các test đó.

## 10. Trạng thái kiểm tra của tài liệu

Code C đã qua kiểm tra cú pháp bằng GCC trên máy tạo tài liệu, với header lấy từ repo và khai báo policy theo hướng dẫn Quân. Chưa build bằng toolchain RISC-V và chưa chạy QEMU trong phiên này, nên chưa có kết quả thực nghiệm PASS.

Giữ phạm vi tuần 5 gọn: không thay vòng đời job nếu chưa phát hiện lỗi, không thêm syscall đo thời gian chỉ để ép kết quả mô hình, và không gọi chương trình workload này là bộ nghiệm thu hoàn chỉnh EDF–RM.
