#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "rttrace.h"
#include "schedpolicy.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];
struct spinlock rt_lock;
uint64 rt_now; // Đồng hồ RT, được bảo vệ bởi rt_lock.

// Hai biến này chỉ được đọc/ghi khi giữ rt_lock sau khi khởi động.
static int rt_policy = SCHED_EDF;
static uint64 rt_policy_generation = 0;

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;
// Caller must NOT hold rt_lock; may hold p->lock.
static void
rt_reset(struct proc *p)
{
  acquire(&rt_lock);                // Lấy khóa bảo vệ metadata RT.
  memset(&p->rt, 0, sizeof(p->rt)); // Đưa các trường RT về 0.
  release(&rt_lock);                // Nhả khóa.
}

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  initlock(&rt_lock, "rt_lock");
  rt_now = 0;
  for (p = proc; p < &proc[NPROC]; p++) {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  rt_reset(p); // Tiến trình mới bắt đầu chưa đăng ký RT.
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  rt_reset(p); // Dọn metadata trước khi tải sử dụng ô tiến trình
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline,
               PTE_R | PTE_X) < 0) {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)(p->trapframe),
               PTE_R | PTE_W) < 0) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0) {
    if (sz + n > TRAPFRAME) {
      return -1;
    }
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if (n < 0) {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0) {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++) {
    if (pp->parent == p) {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd]) {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;
  rt_reset(p); // Xóa đăng ký RT trước ZOMBIE

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;) {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++) {
      if (pp->parent == p) {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE) {
          // Found one.
          pid = pp->pid;
          if (addr != 0 &&
              copyout(p->pagetable, p->sz, addr, (char *)&pp->xstate,
                      sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          pp->parent = 0;
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p)) {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep_prepare(p); //DOC: wait-sleep
    release(&wait_lock);
    sleep();
    acquire(&wait_lock);
  }
}
// Chuyển tiến trình hiện tại về CPU 0.
// Bên gọi không được giữ spinlock nào.
void
rt_bind_cpu0(void)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  acquire(&rt_lock);
  p->rt.pinned = 1;
  release(&rt_lock);

  if (cpuid() != RT_CPU) {
    p->state = RUNNABLE;

    // Chỉ giữ p->lock; scheduler CPU 0 sẽ chọn lại tiến trình.
    sched();
  }

  release(&p->lock);
}

// Gỡ ràng buộc CPU nếu đăng ký thất bại và chưa active.
// Bên gọi phải nhả rt_lock trước khi gọi.
void
rt_unbind_if_inactive(void)
{
  struct proc *p = myproc();

  acquire(&rt_lock);
  if (!p->rt.active)
    p->rt.pinned = 0;
  release(&rt_lock);
}

int
rt_get_policy(void)
{
  acquire(&rt_lock);
  int policy = rt_policy;
  release(&rt_lock);
  return policy;
}

// new
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

// Giá trị nhỏ hơn được ưu tiên trước; NONE là không đủ điều kiện.
enum dispatch_class {
  DISPATCH_NONE = -1,
  DISPATCH_KERNEL = 0,
  DISPATCH_RT = 1,
  DISPATCH_NORMAL = 2
};

// Bên gọi phải giữ p->lock rồi rt_lock, theo đúng thứ tự.
static int
rt_dispatch_class_locked(struct proc *p, int cpu)
{
  if (p->state != RUNNABLE)
    return DISPATCH_NONE;

  // RT active hoặc đang chuyển CPU chỉ được chạy trên CPU 0.
  if ((p->rt.pinned || p->rt.active) && cpu != RT_CPU)
    return DISPATCH_NONE;

  // Cho process bị kill tiếp tục chạy để đi vào đường thoát.
  if (p->killed)
    return DISPATCH_KERNEL;

  // Chưa kích hoạt vòng đời job thì xử lý như process thường.
  if (!p->rt.active)
    return DISPATCH_NORMAL;

  // Cho phép hoàn tất đường kernel trước khi chặn workload.
  if (p->rt.kernel_resume)
    return DISPATCH_KERNEL;

  // Job được chạy khi đã release, READY và còn budget.
  if (p->rt.enabled && p->rt.job_state == RT_JOB_READY && p->rt.remaining > 0 &&
      p->rt.release <= rt_now)
    return DISPATCH_RT;

  return DISPATCH_NONE;
}
// Trả ứng viên với p->lock đang được giữ, không giữ rt_lock.
// Trả 0 nếu không tìm được process đủ điều kiện.
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
          better = key < best_key || (key == best_key && p->pid < best_pid);
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

    // Kiểm tra lại vì ứng viên có thể đã đổi sau lần quét.
    acquire(&best->lock);
    acquire(&rt_lock);

    int cls = rt_dispatch_class_locked(best, cpu);
    int valid = generation == rt_policy_generation && best->pid == best_pid &&
                cls == best_class;

    if (valid && cls == DISPATCH_RT) {
      uint64 key =
        policy == SCHED_RM ? (uint64)best->rt.period : best->rt.deadline;
      valid = key == best_key && best->rt.job_id == best_job;
    }

    release(&rt_lock);

    if (valid)
      return best;

    release(&best->lock);

    // Ứng viên không còn hợp lệ thì quét lại.
  }
}
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct cpu *c = mycpu();
  int cursor = 0;

  c->proc = 0;

  for (;;) {
    intr_on();
    intr_off();

    struct proc *p = rt_pick_next(cpuid(), cursor);

    if (p == 0) {
      asm volatile("wfi");
      continue;
    }

    // Bắt đầu lượt quét sau từ ô kế tiếp để luân phiên process thường.
    cursor = ((int)(p - proc) + 1) % NPROC;

    // Bộ chọn trả về khi chỉ đang giữ p->lock.
    p->state = RUNNING;
    c->proc = p;

    // rt_pick_next đã trả về p với p->lock đang được giữ.
    // Lấy thêm rt_lock theo thứ tự p->lock rồi rt_lock.
    acquire(&rt_lock);

    if (p->rt.active) {
      int cls;

      // Phân biệt chạy tiếp đường kernel với chạy workload EDF.
      if (p->killed || p->rt.kernel_resume)
        cls = RT_TRACE_KERNEL;
      else
        cls = rt_policy == SCHED_RM ? RT_TRACE_RM : RT_TRACE_EDF;

      rt_trace_emit_locked(p, RT_EV_DISPATCH, cls);
    } else if (!p->killed) {
      rt_trace_emit_locked(p, RT_EV_NORMAL, 0);
    }

    // Phải nhả rt_lock trước khi chuyển ngữ cảnh.
    release(&rt_lock);

    swtch(&c->context, &p->context);

    // Giữ quy ước ngắt của baseline xv6 hiện tại.
    mycpu()->intena = 0;
    c->proc = 0;

    release(&p->lock);
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched RUNNING");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    first = 0;

    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){"/init", 0});
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Register current process as waiting for wakeups on chan.
void
sleep_prepare(void *chan)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  if (chan == 0)
    panic("sleep_prepare: zero chan");
  p->chan = chan;
  release(&p->lock);
}

// Put the thread to sleep.  Assumes sleep_prepare() was called before.
// If the channel registered by sleep_prepare() has been woken up in
// the meantime, do not go to sleep, and instead return immediately.
void
sleep(void)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  if (p->chan != 0) {
    p->state = SLEEPING;
    sched();
  }
  release(&p->lock);
}

// Wake up all processes sleeping on channel chan.
void
wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->chan == chan) {
      // If the process is waiting for wakeups on this channel,
      // signal that the wakeup happened by clearing p->chan.
      p->chan = 0;

      // If this waiting process has gotten so far as to actually
      // go to sleep, also set it back to RUNNING.
      if (p->state == SLEEPING) {
        p->state = RUNNABLE;
      }
    }
    release(&p->lock);
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  if (pid == 0)
    return -1;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid) {
      p->killed = 1;
      if (p->state == SLEEPING) {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst) {
    return copyout(p->pagetable, p->sz, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src) {
    return copyin(p->pagetable, p->sz, dst, src, len);
  } else {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
    // clang-format off
    [UNUSED]    = "unused",
    [USED]      = "used",
    [SLEEPING]  = "sleep ",
    [RUNNABLE]  = "runble",
    [RUNNING]   = "run   ",
    [ZOMBIE]    = "zombie"
    // clang-format on
  };
  struct proc *p;
  char *state;

  printk("\n");
  for (p = proc; p < &proc[NPROC]; p++) {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printk("%d %s %s", p->pid, state, p->name);
    printk("\n");
  }
}
