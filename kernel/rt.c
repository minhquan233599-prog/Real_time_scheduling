#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "rttrace.h"

// Khởi tạo một job mới.
// Hàm gọi phải đang giữ rt_lock.
// Hàm này không lấy p->lock và không chuyển ngữ cảnh.
static void
rt_release_locked(struct proc *p, uint64 release)
{
  p->rt.release = release;
  p->rt.deadline = release + (uint64)p->rt.period;
  p->rt.next_release = p->rt.deadline;

  p->rt.remaining = p->rt.runtime;
  p->rt.job_state = RT_JOB_READY;
  p->rt.job_id++;
  p->rt.miss_reported = 0;

  // Ghi sau khi metadata của job mới đã hoàn chỉnh.
  rt_trace_emit_locked(p, RT_EV_RELEASE, 0);
}

// Khởi tạo đăng ký RT sau khi admission thành công.
// Tiến trình phải được chuyển về CPU 0 trước khi gọi.
// Hàm gọi phải đang giữ rt_lock.
void
rt_start_locked(struct proc *p, int period, int runtime)
{
  p->rt.enabled = 1;
  p->rt.period = period;
  p->rt.runtime = runtime;

  p->rt.active = 1;
  p->rt.pinned = 1;

  // Syscall đăng ký vẫn đang chạy trong kernel.
  p->rt.kernel_resume = 1;

  p->rt.job_id = 0;
  p->rt.deadline_misses = 0;
  p->rt.miss_reported = 0;

  rt_release_locked(p, rt_now);
}

// Đánh dấu tiến trình đang xử lý trap hoặc syscall.
// Gọi khi vừa vào usertrap, trước khi bật lại ngắt.
void
rt_kernel_enter(void)
{
  struct proc *p = myproc();

  acquire(&rt_lock);
  p->rt.kernel_resume = 1;
  release(&rt_lock);
}

// Kiểm tra tiến trình hiện tại đã kích hoạt EDF hay chưa.
int
rt_current_active(void)
{
  struct proc *p = myproc();
  int active;

  acquire(&rt_lock);
  active = p->rt.active;
  release(&rt_lock);

  return active;
}

// Chỉ gọi từ timer của CPU 0, khi ngắt đang bị tắt.
// Không gọi khi đang giữ tickslock hoặc khóa tiến trình.
void
rt_clock_tick(void)
{
  if (cpuid() != RT_CPU)
    return;

  struct proc *running = myproc();

  acquire(&rt_lock);

  rt_now++;

  // Tính một tick cho tác vụ RT đang chạy tại thời điểm lấy mẫu.
  // Không trừ budget của tiến trình đang ngủ hoặc chờ CPU.
  if (running != 0 && running->rt.active &&
      running->rt.job_state == RT_JOB_READY &&
      running->rt.remaining > 0) {

    int before = running->rt.remaining;
    running->rt.remaining--;

    if (running->rt.remaining == 0)
      running->rt.job_state = RT_JOB_THROTTLED;

    // aux lưu budget trước khi trừ.
    rt_trace_emit_locked(running, RT_EV_TICK, before);

    if (running->rt.remaining == 0)
      rt_trace_emit_locked(running, RT_EV_BUDGET0, 0);
  }

  for (struct proc *p = proc; p < &proc[NPROC]; p++) {
    if (!p->rt.active || !p->rt.enabled)
      continue;

    int unfinished = p->rt.job_state == RT_JOB_READY ||
                     p->rt.job_state == RT_JOB_THROTTLED;

    // Job chưa báo hoàn thành khi đã đến deadline.
    if (unfinished && rt_now >= p->rt.deadline &&
        !p->rt.miss_reported) {
      p->rt.deadline_misses++;
      p->rt.miss_reported = 1;

      // Ghi ngay sau khi cập nhật bộ đếm miss.
      rt_trace_emit_locked(p, RT_EV_MISS, 0);
    }

    if (rt_now < p->rt.next_release)
      continue;

    uint64 boundary = p->rt.next_release;

    if (p->rt.job_state == RT_JOB_COMPLETE ||
        p->rt.job_state == RT_JOB_WAIT_RELEASE) {
      // Job cũ đã xong: bắt đầu job mới tại mốc chu kỳ.
      rt_release_locked(p, boundary);
    } else if (unfinished) {
      // Job chưa xong: giữ nguyên job_id, release và deadline.
      p->rt.remaining = p->rt.runtime;
      p->rt.job_state = RT_JOB_READY;
      p->rt.next_release = boundary + (uint64)p->rt.period;

      // Ghi sau khi cấp lại budget, trước khi nhả rt_lock.
      rt_trace_emit_locked(p, RT_EV_REFILL, 0);
    }
  }

  release(&rt_lock);
}

// Đánh dấu hoàn thành job hiện tại.
// Việc chờ kỳ tiếp theo diễn ra ở rt_prepare_user_return().
int
rt_finish_current_job(void)
{
  struct proc *p = myproc();

  acquire(&rt_lock);

  if (!p->rt.active || !p->rt.enabled ||
      (p->rt.job_state != RT_JOB_READY &&
       p->rt.job_state != RT_JOB_THROTTLED)) {
    release(&rt_lock);
    return -1;
  }

  p->rt.job_state = RT_JOB_COMPLETE;
  p->rt.remaining = 0;

  // Ghi trạng thái sau khi job đã được đánh dấu hoàn thành.
  rt_trace_emit_locked(p, RT_EV_COMPLETE, 0);

  release(&rt_lock);

  return 0;
}

// Gọi trước khi chuẩn bị trở về user, khi ngắt đã bị tắt.
// Hàm gọi không được giữ spinlock.
// Thứ tự lấy khóa luôn là p->lock rồi rt_lock.
void
rt_prepare_user_return(void)
{
  struct proc *p = myproc();
  int scheduled = 0;

  for (;;) {
    acquire(&p->lock);
    acquire(&rt_lock);

    if (p->killed) {
      // Cho phép đường thoát tiến trình hoàn tất trong kernel.
      p->rt.kernel_resume = 1;

      release(&rt_lock);
      release(&p->lock);

      kexit(-1);
    }

    // Đây là điểm an toàn để áp dụng trạng thái job và budget.
    p->rt.kernel_resume = 0;

    int active = p->rt.active;

    int ready = p->rt.enabled &&
                p->rt.job_state == RT_JOB_READY &&
                p->rt.remaining > 0 &&
                p->rt.release <= rt_now;

    // Tiến trình thường được trở về user ngay.
    // Tác vụ RT phải qua ít nhất một lượt chọn EDF trước khi trở về.
    if (!active || (ready && scheduled)) {
      if (active && cpuid() != RT_CPU)
        panic("rt return cpu");

      release(&rt_lock);
      release(&p->lock);
      return;
    }

    release(&rt_lock);

    // Chỉ còn giữ p->lock trước khi gọi sched().
    // Scheduler của Quân sẽ bỏ qua job chưa đủ điều kiện.
    p->state = RUNNABLE;
    sched();

    release(&p->lock);

    // Sau khi được chọn lại, kiểm tra trạng thái và kill một lần nữa.
    scheduled = 1;
  }
}