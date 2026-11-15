#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// Bên gọi đang giữ rt_lock; không lấy p->lock ở đây.
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
}

// Gọi sau admission thành công, trên CPU 0 và đang giữ rt_lock.
void
rt_start_locked(struct proc *p, int period, int runtime)
{
  p->rt.enabled = 1;
  p->rt.period = period;
  p->rt.runtime = runtime;
  p->rt.active = 1;
  p->rt.pinned = 1;
  p->rt.kernel_resume = 1; // Syscall đăng ký vẫn đang chạy trong kernel.
  p->rt.job_id = 0;
  p->rt.deadline_misses = 0;
  p->rt.miss_reported = 0;
  rt_release_locked(p, rt_now);
}

// Gọi khi vừa vào usertrap, trước khi bật lại ngắt.
void
rt_kernel_enter(void)
{
  struct proc *p = myproc();
  acquire(&rt_lock);
  p->rt.kernel_resume = 1;
  release(&rt_lock);
}

// Dùng để phân biệt tác vụ RT với tiến trình thường ở đường timer.
int
rt_current_active(void)
{
  struct proc *p = myproc();
  acquire(&rt_lock);
  int active = p->rt.active;
  release(&rt_lock);
  return active;
}

// Chỉ gọi từ timer CPU 0 khi ngắt đang tắt.
void
rt_clock_tick(void)
{
  if (cpuid() != RT_CPU)
    return;

  struct proc *running = myproc();
  acquire(&rt_lock);
  rt_now++;

  // Chỉ tiến trình đang chạy tại tick lấy mẫu bị trừ budget.
  if (running != 0 && running->rt.active &&
      running->rt.job_state == RT_JOB_READY &&
      running->rt.remaining > 0) {
    running->rt.remaining--;
    if (running->rt.remaining == 0)
      running->rt.job_state = RT_JOB_THROTTLED;
  }

  for (struct proc *p = proc; p < &proc[NPROC]; p++) {
    if (!p->rt.active || !p->rt.enabled)
      continue;

    int unfinished = p->rt.job_state == RT_JOB_READY ||
                     p->rt.job_state == RT_JOB_THROTTLED;

    // Mỗi job chỉ ghi nhận một lần trễ deadline.
    if (unfinished && rt_now >= p->rt.deadline &&
        !p->rt.miss_reported) {
      p->rt.deadline_misses++;
      p->rt.miss_reported = 1;
    }

    if (rt_now < p->rt.next_release)
      continue;

    uint64 boundary = p->rt.next_release;

    if (p->rt.job_state == RT_JOB_COMPLETE ||
        p->rt.job_state == RT_JOB_WAIT_RELEASE) {
      // Job cũ đã xong: tạo job mới tại mốc chu kỳ.
      rt_release_locked(p, boundary);
    } else if (unfinished) {
      // Job chưa xong: cấp lại budget nhưng giữ nguyên job cũ.
      p->rt.remaining = p->rt.runtime;
      p->rt.job_state = RT_JOB_READY;
      p->rt.next_release = boundary + (uint64)p->rt.period;
    }
  }

  release(&rt_lock);
}

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
  release(&rt_lock);
  return 0;
}

// Gọi khi ngắt đã tắt, trước khi đổi vector trap sang uservec.
// Bên gọi không được giữ spinlock.
void
rt_prepare_user_return(void)
{
  struct proc *p = myproc();
  int scheduled = 0;

  for (;;) {
    acquire(&p->lock);
    acquire(&rt_lock);

    if (p->killed) {
      p->rt.kernel_resume = 1; // Cho đường thoát kernel hoàn tất.
      release(&rt_lock);
      release(&p->lock);
      kexit(-1);
    }

    p->rt.kernel_resume = 0; // Đã tới điểm an toàn để chặn workload.
    int active = p->rt.active;
    int ready = p->rt.enabled &&
                p->rt.job_state == RT_JOB_READY &&
                p->rt.remaining > 0 &&
                p->rt.release <= rt_now;

    if (!active || (ready && scheduled)) {
      if (active && cpuid() != RT_CPU)
        panic("rt return cpu");
      release(&rt_lock);
      release(&p->lock);
      return;
    }

    release(&rt_lock);
    p->state = RUNNABLE;
    sched(); // Chỉ còn giữ p->lock; scheduler lọc theo trạng thái RT.
    release(&p->lock);
    scheduled = 1;
  }
}
