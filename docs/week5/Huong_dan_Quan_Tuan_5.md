# Tuần 5 — Code và kiểm tra phần Quân

## 1. Phạm vi và giao diện thống nhất

Bổ sung RM vào scheduler tuần 4, đồng thời cho phép chọn EDF/RM bằng syscall. Tài liệu dựa trên các file `main` đọc ngày 18/11/2026 tại https://github.com/minhquan233599-prog/Real_time_scheduling. Không chỉnh sửa repository trực tuyến.

Mốc đối chiếu: blob `kernel/proc.c`: `e99f9e153d6778c25cb71b2af237ef46ad247f2b`; `kernel/sysproc.c`: `777a5f659999ff5b5ed557c863c17e37c8e1da92`. Nếu main đã thay đổi, đối chiếu vị trí theo tên hàm, không theo số dòng.

| Quy ước | Giá trị / hành vi |
|---|---|
| Chính sách | `SCHED_EDF = 0`, `SCHED_RM = 1`; mặc định EDF |
| `sched_getpolicy()` | Trả chính sách hiện hành |
| `sched_setpolicy(policy)` | 0: thành công; -1: policy không hợp lệ; -2: đang có đăng ký RT hoặc đang chuyển CPU để đăng ký |
| Đặt lại cùng policy | Trả 0, không thay metadata hay budget, kể cả đang có RT |
| EDF | Deadline tuyệt đối nhỏ nhất; bằng nhau chọn PID nhỏ hơn |
| RM | Chu kỳ T nhỏ nhất; bằng nhau chọn PID nhỏ hơn |
| CPU | RT vẫn chỉ chạy CPU 0; kiểm thử trên CPUS=1 và CPUS=4 |

Chính sách là toàn hệ thống, không phải mỗi process. Bản này dành cho xv6 học tập, chưa thêm phân quyền đổi policy. Admission hiện có vẫn chỉ giới hạn tổng C/T ≤ 1; **chưa bảo đảm tập tác vụ khả thi dưới RM**. Tuần này dùng các bộ tác vụ đã kiểm tra; không gọi đây là admission RM hoàn chỉnh.

## 2. Danh sách file

- Tạo `kernel/schedpolicy.h`.
- Sửa `kernel/proc.c`, `kernel/defs.h`, `kernel/sysproc.c`.
- Sửa `kernel/syscall.h`, `kernel/syscall.c`, `user/user.h`, `user/usys.pl`.
- Sửa nhỏ `kernel/rttrace.h` và đoạn ghi DISPATCH trong `scheduler()` để trace phân biệt RM với EDF.
- Test riêng: `user/rt_policytest.c`, thêm vào `Makefile`. Dũng có thể sở hữu file này trong phân công nhóm; Quân dùng để tự kiểm tra giao diện trước khi bàn giao. Không tạo thêm một file test tương tự.

## 3. Header chính sách — tạo kernel/schedpolicy.h

```c
#ifndef XV6_SCHEDPOLICY_H
#define XV6_SCHEDPOLICY_H

// Chính sách chung cho kernel và chương trình user.
#define SCHED_EDF 0
#define SCHED_RM  1

#endif
```

Header chỉ chứa hằng số, không định nghĩa biến toàn cục. User include bằng `"kernel/schedpolicy.h"`; kernel include bằng `"schedpolicy.h"`.

## 4. Quản lý policy — sửa kernel/proc.c

### 4.1. Include và biến

Thêm ở nhóm include đầu file:

```c
#include "schedpolicy.h"
```

Ngay sau khai báo `uint64 rt_now;`, thêm:

```c
// Hai biến này chỉ được đọc/ghi khi giữ rt_lock sau khi khởi động.
static int rt_policy = SCHED_EDF;
static uint64 rt_policy_generation = 0;
```

Không đặt các biến này trong `struct rt_meta`: mọi tác vụ RT dùng chung một policy.

### 4.2. Thêm hai hàm trước enum dispatch_class

```c
int
rt_get_policy(void)
{
  acquire(&rt_lock);
  int policy = rt_policy;
  release(&rt_lock);
  return policy;
}

int
rt_set_policy(int policy)
{
  if (policy != SCHED_EDF && policy != SCHED_RM)
    return -1;

  acquire(&rt_lock);

  // Cùng chính sách thì không thay đổi gì, đặc biệt không reset budget.
  if (policy == rt_policy) {
    release(&rt_lock);
    return 0;
  }

  // Quét metadata dưới cùng khóa với đăng ký và thu hồi RT.
  // Không lấy p->lock khi đã giữ rt_lock.
  for (struct proc *p = proc; p < &proc[NPROC]; p++) {
    if (p->rt.enabled || p->rt.active || p->rt.pinned) {
      release(&rt_lock);
      return -2;
    }
  }

  rt_policy = policy;
  // Phát hiện cả trường hợp đổi EDF -> RM -> EDF trong một lần quét.
  rt_policy_generation++;
  release(&rt_lock);
  return 0;
}
```

Kiểm tra và cập nhật policy trong cùng vùng khóa tránh tình huống vừa kiểm tra “không có RT” thì CPU khác đăng ký xen vào. `pinned` còn chặn đổi policy khi tiến trình đang di chuyển về CPU 0 để đăng ký. Đăng ký chưa bắt đầu chuyển CPU sẽ dùng policy đang có khi được chấp nhận.

### 4.3. Đổi tên lớp RT trong proc.c

Trong **riêng `kernel/proc.c`**, đổi mọi `DISPATCH_EDF` thành `DISPATCH_RT`. Enum thành:

```c
enum dispatch_class {
  DISPATCH_NONE = -1,
  DISPATCH_KERNEL = 0,
  DISPATCH_RT = 1,
  DISPATCH_NORMAL = 2
};
```

Trong `rt_dispatch_class_locked()`, dòng cuối trả lớp tác vụ đổi thành:

```c
  if (p->rt.enabled && p->rt.job_state == RT_JOB_READY &&
      p->rt.remaining > 0 && p->rt.release <= rt_now)
    return DISPATCH_RT;
```

Các nhánh còn lại giữ logic hiện có. Lớp này chỉ biểu thị “RT đủ điều kiện”; việc ưu tiên theo EDF hay RM nằm trong bộ chọn. Không đổi `RT_TRACE_EDF` bằng tìm-thay toàn dự án.

### 4.4. Thay toàn bộ rt_pick_next() bằng hàm sau

```c
// Trả tiến trình với p->lock đang giữ; không giữ rt_lock khi trả về.
static struct proc *
rt_pick_next(int cpu, int start)
{
  for (;;) {
    struct proc *best = 0;
    int best_class = DISPATCH_NONE;
    int best_pid = 0;
    uint64 best_key = 0;
    uint64 best_job = 0;

    // Mỗi lượt quét dùng một policy thống nhất.
    acquire(&rt_lock);
    int policy = rt_policy;
    uint64 generation = rt_policy_generation;
    release(&rt_lock);

    for (int n = 0; n < NPROC; n++) {
      struct proc *p = &proc[(start + n) % NPROC];
      acquire(&p->lock);
      acquire(&rt_lock);

      int cls = rt_dispatch_class_locked(p, cpu);
      uint64 key = 0;
      int better = 0;

      if (cls == DISPATCH_RT) {
        // RM dùng chu kỳ; EDF dùng deadline tuyệt đối.
        key = policy == SCHED_RM ? (uint64)p->rt.period : p->rt.deadline;
      }

      if (cls != DISPATCH_NONE) {
        if (best == 0 || cls < best_class) {
          better = 1;
        } else if (cls == DISPATCH_RT && best_class == DISPATCH_RT) {
          // Hòa ưu tiên thì dùng PID để có kết quả ổn định.
          better = key < best_key ||
                   (key == best_key && p->pid < best_pid);
        }
      }

      if (better) {
        best = p;
        best_class = cls;
        best_pid = p->pid;
        best_key = key;
        best_job = p->rt.job_id;
      }

      release(&rt_lock);
      release(&p->lock);
    }

    if (best == 0) {
      acquire(&rt_lock);
      int changed = generation != rt_policy_generation;
      release(&rt_lock);
      if (changed)
        continue;
      return 0;
    }

    // Khóa lại ứng viên vì trạng thái có thể đổi trong lúc quét.
    acquire(&best->lock);
    acquire(&rt_lock);

    int cls = rt_dispatch_class_locked(best, cpu);
    int valid = generation == rt_policy_generation &&
                best->pid == best_pid && cls == best_class;

    if (valid && cls == DISPATCH_RT) {
      uint64 key = policy == SCHED_RM ?
                     (uint64)best->rt.period : best->rt.deadline;
      valid = key == best_key && best->rt.job_id == best_job;
    }

    release(&rt_lock);
    if (valid)
      return best;

    release(&best->lock);
    // Ứng viên hoặc policy thay đổi thì quét lại.
  }
}
```

`best_key` là đại lượng ưu tiên: deadline đối với EDF, T đối với RM. `generation` tránh dùng kết quả quét theo policy cũ. Đây vẫn là cách quét từng process của tuần 4, không phải một ảnh chụp nguyên tử toàn bảng process; bản mở rộng không giải quyết mọi biến động ứng viên trong lúc quét.

## 5. Trace phải ghi đúng chính sách

### 5.1. Sửa kernel/rttrace.h

Thay enum `rt_trace_dispatch` hiện có bằng:

```c
// Phân biệt đường kernel với workload chạy theo từng chính sách.
enum rt_trace_dispatch {
  RT_TRACE_KERNEL = 0,
  RT_TRACE_EDF = 1,
  RT_TRACE_RM = 2
};
```

Trong comment của trường `aux`, sửa thành `// DISPATCH: lớp KERNEL, EDF hoặc RM.` Không đổi layout struct hay số event khác.

### 5.2. Sửa đoạn phân loại trong scheduler() của proc.c

Trong nhánh `if (p->rt.active)`, đang giữ `rt_lock`, thay phần gán `cls` bằng:

```c
      // Kernel continuation có thể cần chạy để hoàn tất syscall hoặc thoát.
      if (p->killed || p->rt.kernel_resume)
        cls = RT_TRACE_KERNEL;
      else
        cls = rt_policy == SCHED_RM ? RT_TRACE_RM : RT_TRACE_EDF;

      rt_trace_emit_locked(p, RT_EV_DISPATCH, cls);
```

Không gọi `rt_get_policy()` ở đây vì đang giữ `rt_lock`; gọi lại hàm lấy khóa sẽ gây lỗi. Nhánh NORMAL và các thao tác nhả khóa giữ nguyên. Cuối scheduler vẫn phải có:

```c
    release(&rt_lock);
    swtch(&c->context, &p->context);

    // Giữ quy ước ngắt và bàn giao khóa của bản xv6 hiện tại.
    mycpu()->intena = 0;
    c->proc = 0;
    release(&p->lock);
```

Đoạn trên chỉ nhắc lại vị trí, không thêm lần nhả khóa thứ hai. Thành cần dùng `RT_TRACE_RM` khi kiểm tra dispatch RM; bộ EDF cũ tiếp tục dùng `RT_TRACE_EDF`.

## 6. Nối hai syscall

### kernel/defs.h — thêm cạnh các khai báo proc.c

```c
int rt_get_policy(void);
int rt_set_policy(int);
```

### kernel/sysproc.c — thêm cuối file

```c
uint64
sys_sched_setpolicy(void)
{
  int policy;
  // Đọc đối số thứ nhất từ chương trình user.
  argint(0, &policy);
  return (uint64)rt_set_policy(policy);
}

uint64
sys_sched_getpolicy(void)
{
  return (uint64)rt_get_policy();
}
```

### kernel/syscall.h — thêm sau syscall 26

```c
#define SYS_sched_setpolicy 27
#define SYS_sched_getpolicy 28
```

Số 27–28 đang trống ở bản main đã đọc. Nếu nhóm đã thêm syscall khác, kiểm tra lại trước khi dùng.

### kernel/syscall.c — thêm vào nhóm extern

```c
extern uint64 sys_sched_setpolicy(void);
extern uint64 sys_sched_getpolicy(void);
```

Thêm vào mảng `syscalls[]`, trước dấu `};`:

```c
  [SYS_sched_setpolicy] = sys_sched_setpolicy,
  [SYS_sched_getpolicy] = sys_sched_getpolicy,
```

### user/user.h — thêm vào nhóm syscall

```c
int sched_setpolicy(int);
int sched_getpolicy(void);
```

### user/usys.pl — thêm cuối danh sách entry

```perl
entry("sched_setpolicy");
entry("sched_getpolicy");
```

Không sửa `user/usys.S`: file đó được sinh tự động. File Perl dùng cú pháp comment `#` nếu cần, không dùng `//` như C.

## 7. Test riêng — user/rt_policytest.c

Test này kiểm tra giao diện policy, chuyển qua lại, lỗi đầu vào và chặn đổi khi có RT. Dùng pipe để giữ một tiến trình RT còn tồn tại đến khi cha kiểm tra xong. Cha vẫn là tiến trình thường.

```c
#include "kernel/types.h"
#include "kernel/schedpolicy.h"
#include "user/user.h"

static int
expect(char *name, int actual, int expected)
{
  int ok = actual == expected;
  printf("%s: expected=%d actual=%d [%s]\n",
         name, expected, actual, ok ? "PASS" : "FAIL");
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
  int restored = expect("restore original policy", sched_setpolicy(original), 0);
  int all_ok = passed == runs && restored;
  printf("POLICY RESULT: %d/%d %s\n", passed, runs,
         all_ok ? "PASS" : "FAIL");
  exit(all_ok ? 0 : 1);
}
```

Trong `Makefile`, thêm đúng một dòng vào `UPROGS`:

```make
	$U/_rt_policytest\
```

Nếu đây là dòng cuối danh sách, có thể bỏ dấu `\`. Không thêm object của chương trình user vào `OBJS` kernel. Thành đang phụ trách Makefile khi tích hợp; Quân có thể thêm dòng này trên nhánh riêng để test, rồi thống nhất khi merge.

## 8. Hướng dẫn build và lưu minh chứng

### 8.1. Trên terminal Ubuntu, tại thư mục gốc dự án

```bash
mkdir -p docs/week5/logs

git rev-parse HEAD > docs/week5/logs/quan-version.txt
git status --short >> docs/week5/logs/quan-version.txt
qemu-system-riscv64 --version >> docs/week5/logs/quan-version.txt
riscv64-unknown-elf-gcc --version >> docs/week5/logs/quan-version.txt
clang-format --version >> docs/week5/logs/quan-version.txt
```

Lưu hết file trong VS Code trước khi chạy. Format các file vừa thêm/sửa, dùng phiên bản clang-format tương thích CI:

```bash
clang-format -i kernel/schedpolicy.h kernel/proc.c kernel/defs.h \
  kernel/sysproc.c kernel/syscall.h kernel/syscall.c kernel/rttrace.h \
  user/user.h user/rt_policytest.c

clang-format --dry-run --Werror kernel/schedpolicy.h kernel/proc.c \
  kernel/defs.h kernel/sysproc.c kernel/syscall.h kernel/syscall.c \
  kernel/rttrace.h user/user.h user/rt_policytest.c
```

Không đưa `user/usys.pl` vào clang-format. Build và lưu đúng mã thoát của make trong Bash:

```bash
set -o pipefail
make kernel/kernel fs.img 2>&1 | tee docs/week5/logs/quan-build.log
build_rc=${PIPESTATUS[0]}
echo "BUILD_EXIT_CODE=$build_rc" | tee -a docs/week5/logs/quan-build.log
```

Chỉ chạy QEMU khi `BUILD_EXIT_CODE=0`. Nếu build thất bại, sửa lỗi trước; không dùng log từ binary cũ làm minh chứng.

### 8.2. CPUS=1

Trên Ubuntu:

```bash
script -q -e -c 'make qemu CPUS=1' docs/week5/logs/quan-policy-cpus1.log
```

Trong shell xv6, chạy lần lượt:

```text
rt_policytest 10
rt_job_smoke
rttest
rt_edftest 1
```

Khởi động QEMU mới mặc định EDF; policytest khôi phục EDF sau khi chạy. Mong đợi policy 10/10, smoke PASS, rttest 6/6 và EDF 1/1 PASS. Đây là kết quả mong đợi, chưa phải kết quả đã đo.

Thoát QEMU bằng **Ctrl+A, nhả ra rồi X**.

### 8.3. CPUS=4

Trên Ubuntu:

```bash
script -q -e -c 'make qemu CPUS=4' docs/week5/logs/quan-policy-cpus4.log
```

Trong xv6 chạy lại đúng bốn lệnh trên rồi thoát QEMU. Không mở hai QEMU cùng dùng một `fs.img`.

Lọc nhanh kết quả trên Ubuntu:

```bash
grep -nE 'POLICY RESULT|RT JOB SMOKE|RESULT|FAIL|panic' \
  docs/week5/logs/quan-policy-cpus1.log \
  docs/week5/logs/quan-policy-cpus4.log
```

Grep chỉ giúp xem nhanh; đọc các dòng FAIL và kết quả cuối thực tế. `script -e` không tự hiểu các dòng PASS/FAIL của test trong QEMU. Nếu treo hoặc thoát sớm, ghi nhận FAIL/không hoàn tất, không xem là PASS.

Nếu chưa commit code, lưu thêm patch các file tracked và giữ các file mới cùng báo cáo. Sau khi commit, ghi mã commit thực tế tương ứng với bản đã test. Không ghi một commit cũ như thể đã chứa các sửa đổi chưa commit.

## 9. Phạm vi minh chứng và bàn giao

| Kiểm tra | Test trong tài liệu này |
|---|---|
| Chọn/đọc EDF và RM | Có |
| Mã policy sai | Có |
| Không đổi policy khi còn RT, kể cả RT đang chờ pipe | Có |
| Đổi được sau khi con thoát | Có |
| Cùng policy trả 0 | Có; không đo trực tiếp budget để chứng minh không reset |
| Lặp 10 lần trên CPUS=1 và 4 | Có hướng dẫn |
| Hồi quy EDF và syscall cơ bản | Chạy lại chương trình tuần 4 |
| RM chọn đúng T, hòa PID, preemption và fallback | Cần `rt_rmtest` của Thành; policytest chưa chứng minh các tính chất này |
| Race đổi policy với đăng ký đồng thời trên nhiều CPU | Chưa có test chuyên biệt trong smoke này |
| Tài nguyên, usertests, grind, benchmark và Gantt | Nghiệm thu chung tuần 5; không nhân bản toàn bộ vào test của Quân |

Quân bàn giao: code scheduler/syscall, giao diện đã chốt, log build và hai log CPU. Dũng dùng giao diện này cho workload và bổ sung policytest nếu cần; Thành viết checker RM riêng, chú ý `aux == RT_TRACE_RM`.

**Trạng thái tài liệu:** đã đối chiếu cấu trúc với mã nguồn GitHub; code đề xuất chưa được build/chạy trên QEMU trong phiên tạo tài liệu này. Chỉ ghi PASS vào báo cáo sau khi chạy và có log tương ứng. RM scheduler chỉ được nghiệm thu đầy đủ sau khi test thứ tự và preemption RM cũng đạt.
