#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "rttrace.h"

#define RT_TRACE_CAPACITY 4096

// Bộ đệm tĩnh: không cấp phát bộ nhớ trong timer.
static struct rt_trace_event trace_buffer[RT_TRACE_CAPACITY];

// Toàn bộ trạng thái trace được bảo vệ bằng rt_lock.
static int trace_enabled;
static int trace_count;
static int trace_lost;

// Bên gọi bắt buộc đang giữ rt_lock.
// Hàm này không tự lấy khóa và không gọi printf.
void
rt_trace_emit_locked(struct proc *p, int type, int aux)
{
  if (!holding(&rt_lock))
    panic("rt trace lock");

  if (!trace_enabled ||
      (!p->rt.active && type != RT_EV_NORMAL))
    return;

  // Khi đầy, bỏ bản ghi mới và đếm số bản ghi bị mất.
  // Không ghi đè những bản ghi đã lưu.
  if (trace_count >= RT_TRACE_CAPACITY) {
    trace_lost++;
    return;
  }

  struct rt_trace_event *e = &trace_buffer[trace_count];

  e->tick = rt_now;
  e->job_id = p->rt.job_id;
  e->release = p->rt.release;
  e->deadline = p->rt.deadline;
  e->next_release = p->rt.next_release;
  e->misses = p->rt.deadline_misses;

  e->seq = trace_count;
  e->type = type;
  e->cpu = cpuid();
  e->pid = p->pid;

  e->period = p->rt.period;
  e->runtime = p->rt.runtime;
  e->remaining = p->rt.remaining;
  e->kernel_resume = p->rt.kernel_resume;
  e->aux = aux;

  trace_count++;
}

// Điều khiển bắt đầu, dừng và đọc số lượng bản ghi.
uint64
sys_rt_trace_ctl(void)
{
  int command;
  argint(0, &command);

  int result = -1;

  acquire(&rt_lock);

  switch (command) {
  case RT_TRACE_START:
    // Không cho vô tình xóa một phiên đang ghi.
    if (trace_enabled) {
      result = -1;
      break;
    }

    trace_count = 0;
    trace_lost = 0;
    trace_enabled = 1;
    result = 0;
    break;

  case RT_TRACE_STOP:
    trace_enabled = 0;
    result = trace_count;
    break;

  case RT_TRACE_COUNT:
    result = trace_count;
    break;

  case RT_TRACE_LOST:
    result = trace_lost;
    break;

  default:
    result = -1;
    break;
  }

  release(&rt_lock);
  return (uint64)result;
}

// Đọc một bản ghi sau khi đã dừng trace.
// Trả 1: đọc được bản ghi.
// Trả 0: đã hết bản ghi.
// Trả -1: đối số hoặc trạng thái không hợp lệ.
uint64
sys_rt_trace_read(void)
{
  int index;
  uint64 address;
  struct rt_trace_event event;

  argint(0, &index);
  argaddr(1, &address);

  if (index < 0)
    return (uint64)-1;

  acquire(&rt_lock);

  // Chỉ đọc sau khi dừng để dữ liệu không đổi giữa các lần đọc.
  if (trace_enabled) {
    release(&rt_lock);
    return (uint64)-1;
  }

  if (index >= trace_count) {
    release(&rt_lock);
    return 0;
  }

  event = trace_buffer[index];
  release(&rt_lock);

  // Không giữ rt_lock khi sao chép dữ liệu sang user.
  struct proc *p = myproc();

  if (copyout(p->pagetable, p->sz, address,
              (char *)&event, sizeof(event)) < 0)
    return (uint64)-1;

  return 1;
}