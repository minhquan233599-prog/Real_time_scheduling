#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

#define RT_WORDS (NPROC + 1)
// Add C/T to num/den exactly. Return 1 if the new sum is <= 1.

// Called at most NPROC times, with 0 < C <= T <= INT_MAX.
static int
rt_add_fraction(uint *num, uint *den, int period, int runtime)
{
  uint64 carry_num = 0;
  uint64 carry_den = 0;
  for (int i = 0; i < RT_WORDS; i++) {
    uint64 n = (uint64)num[i] * (uint)period
             + (uint64)den[i] * (uint)runtime + carry_num;
    uint64 d = (uint64)den[i] * (uint)period + carry_den;

    num[i] = (uint)n;
    den[i] = (uint)d;

    carry_num = n >> 32;
    carry_den = d >> 32;
  }
  for (int i = RT_WORDS - 1; i >= 0; i--) {
    if (num[i] != den[i])
      return num[i] < den[i];
  }
  return 1;
}

// Caller holds rt_lock. Do not acquire any proc lock here.

static int

rt_admit_locked(struct proc *self, int period, int runtime)

{

  uint num[RT_WORDS] = {0};

  uint den[RT_WORDS] = {1};

  for (int i = 0; i < NPROC; i++) {

    struct proc *q = &proc[i];

    if (q == self || !q->rt.enabled)

      continue;

    if (!rt_add_fraction(num, den, q->rt.period, q->rt.runtime))

      return 0;

  }

  return rt_add_fraction(num, den, period, runtime);

}

uint64

sys_sched_setdeadline(void)

{

  int period, runtime;

  argint(0, &period);

  argint(1, &runtime);

  if (period <= 0 || runtime <= 0 || runtime > period)

    return (uint64)-1;

  struct proc *p = myproc();

  acquire(&rt_lock);

  if (!rt_admit_locked(p, period, runtime)) {

    release(&rt_lock);

    return (uint64)-2;

  }

  p->rt.period = period;

  p->rt.runtime = runtime;

  p->rt.enabled = 1;

  release(&rt_lock);

  return 0;

}