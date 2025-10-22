#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

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

struct proclistnode proclistnodes[NPROCLISTNODE];
struct proclist readylist;

struct channel channels[NCHANNEL];

// implementation step1
// 新宣告 l3, l2, l1 queue
struct proclist l3_queue;
struct sortedproclist l2_queue;
struct sortedproclist l1_queue;


// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
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
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
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
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
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
static struct proc*
allocproc(void)
{
  struct proc *p;
  // 遍歷 proc[] 陣列
  // 找 state == UNUSED 的 slot
  // 找不到就返回 0
  for(p = proc; p < &proc[NPROC]; p++) {
    // 防止多個 CPU 同時選到同一個槽位
    // 找到後保持鎖定，繼續初始化
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  // 分配 pid 並設定狀態
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  // 呼叫 kalloc 分配一個 page 存放 trapframe
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    // 失敗的話清理已分配的資源然後釋放鎖
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  // 分配 pagetable
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    // 失敗的話清理已分配的資源然後釋放鎖
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  // 初始化清空 context
  memset(&p->context, 0, sizeof(p->context));
  // ra = forkret（一個在 Kernel/proc.c 的特殊函數）
  // 新進程第一次被調度時會執行它
  p->context.ra = (uint64)forkret;
  // 設定 sp 為 kernel stack 的頂端
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
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
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
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

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  // uvmfirst 是把 initcode 複製到進程的 virt addr 0
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  // 設定 trapframe （user mode 的起點）
  // 從地址 0 開始執行
  p->trapframe->epc = 0;      // user program counter
  // user stack 在頂端
  p->trapframe->sp = PGSIZE;  // user stack pointer

  // 把 p -> name 設為 initcode
  safestrcpy(p->name, "initcode", sizeof(p->name));
  // 把當前目錄 設為 root
  p->cwd = namei("/");

  // 加入 ready queue
  p->state = RUNNABLE;
  procstatelog(p);
  pushreadylist(p);

  // 釋放鎖
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
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // 設定優先級
  np->priority = 149;
  np->statelogenabled = 0;

  // Copy user memory from parent to child.
  // 透過 uvmcpoy 複製 parent process 的記憶體；失敗的話要清理已分配的資源然後釋放鎖
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  // 複製 trapframe 內容
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  // 設定 子進程 fork() 返回 0
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  // 複製打開的檔案和目錄
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  // 複製名稱
  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  // 設定父子關係
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  procstatelog(np); // Initial state log
  np->state = RUNNABLE;
  procstatelog(np);
  pushreadylist(np);
  release(&np->lock);

  return pid;
}

// Fork but with a priority level for scheduling.
int
priorfork(int priority, int statelogenabled)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  np->priority = priority;
  np->statelogenabled = statelogenabled;

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
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
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  procstatelog(np); // Initial state log
  np->state = RUNNABLE;
  procstatelog(np);
  pushreadylist(np);
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();
  // 一開始先檢查目前正在跑的process是否為初始程序(initproc) 
  // 初始程序不能exit 否則會產生孤兒程序無法被妥善處理
  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  // 檔案系統的事務機制 (Transaction)，確保檔案系統操作的原子性。
  // 如果系統在 iput 過程中崩潰
  // 檔案系統不會處於不一致狀態
  // 要嘛完全完成，要嘛完全不做
  begin_op();
  // iput 是 釋放對 inode 的引用
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  // xstate 儲存退出狀態
  p->xstate = status;
  p->state = ZOMBIE;
  procstatelog(p);

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
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
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    if((p = popreadylist()) == 0) {
      // no runnable processes, waiting...
      continue;
    }
    acquire(&p->lock);
    if(p->state != RUNNABLE) {
      panic("scheduler: p->state != RUNNABLE");
    }
    // Switch to chosen process.  It is the process's job
    // to release its lock and then reacquire it
    // before jumping back to us.
    p->startrunningticks = ticks;
    p->state = RUNNING;
    c->proc = p;
    procstatelog(p);
    
    // implementation step 1
    // 補足 scheduler 中缺少的 swtch
    swtch(&c->context, &p->context);

    // Process is done running for now.
    // It should have changed its p->state before coming back.
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
// 讓 scheduler 取得 CPU
void
sched(void)
{
  int intena;
  struct proc *p = myproc();
  // 確認有沒有取得鎖
  if(!holding(&p->lock))
    panic("sched p->lock");
  // 確保只有一個鎖
  if(mycpu()->noff != 1)
    panic("sched locks");
  // 確認狀態不是 RUNNING
  if(p->state == RUNNING)
    panic("sched running");
  // 確認中斷已經關閉
  // 因為上下文切換期間不能被中斷
  if(intr_get())
    panic("sched interruptible");

  // 透過 intena 保存當前形成的中斷啟用狀態
  intena = mycpu()->intena;
  // 做 switch
  // 儲存當前行程的上下文並切換到 並切換到 CPU 的 scheduler context，使 scheduler 能繼續執行以選擇下一個可執行的行程
  swtch(&p->context, &mycpu()->context);
  // 當這個行程再次被 scheduler 選中，會切換回來這裡，恢復 intena
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  procstatelog(p);
  pushreadylist(p);
  sched();
  release(&p->lock);
}

// Aging
void
aging(void)
{
  // Currently not implemented
}

// Implicit yield is called on timer interrupt
void
implicityield(void)
{
  struct proc *p = myproc();
  // implementation step 1
  // 只有 L3 (priority 0-49) 需要 RR
  if(p->priority >= 0 && p->priority <= 49) {
    // L3 要求每 10 ticks yield
    if(ticks - p->startrunningticks >= 10) {
      yield();
    }
  }
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  struct channel *cn;
  struct proclistnode *pn;
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.
  // mp2: also need to acquire channel lock

  // 獲得 process 鎖
  acquire(&p->lock);  //DOC: sleeplock1
  if((cn = findchannel(chan)) == 0 && (cn = allocchannel(chan)) == 0) {
    panic("sleep: allocchannel");
  }
  // 釋放 tickslock
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  procstatelog(p);

  if((pn = allocproclistnode(p)) == 0) {
    panic("sleep: allocproclistnode");
  }
  pushbackproclist(&cn->pl, pn);
  release(&cn->lock);

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  //下面三個local變數用來存wakeup對象的資料
  struct proc *p; //proc指標: 用來指著正在被喚醒的process
  struct channel *cn; //channel指標: 用來指著對應chan的channel (對應因某個事件被轉到的SLEEPING queue:在我們的例子中為clock interrupt)
  struct proclistnode *pn; //process節點指標: 用來指著channel中被喚醒的processes的節點

  //用findchannel找到對應chan的channel 如果找不到(==0) 直接return 避免作用在非合法的channel
  if((cn = findchannel(chan)) == 0) { 
    // channel not initialized
    return;
  }

  //迴圈把這個chan對應的sleeping queue上的所有processes pop出來並喚醒
  while((pn = popfrontproclist(&cn->pl)) != 0){
    p = pn->p;
    freeproclistnode(pn);
    acquire(&p->lock); //把process的資料先lock住 避免同時有其他cpu在access它
    // Assertions: 在將更新狀態前先將檢查wakeup是否合法
    if(p == myproc()) { //不能wakeup自己
      panic("wakeup: wakeup self");
    }
    if(p->state != SLEEPING) { //只能wakeup處於sleep狀態的process
      panic("wakeup: not sleeping");
    }
    if(p->chan != chan) { //wakeup正確的channel
      panic("wakeup: wrong channel");
    }
    p->state = RUNNABLE;
    procstatelog(p);
    pushreadylist(p);
    release(&p->lock);
  }
  // free channel since it is empty
  cn->used = 0;
  release(&cn->lock);
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;
  struct channel *cn;
  struct proclistnode *pn;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        if((cn = findchannel(p->chan)) == 0) {
          panic("kill findchannel");
        }
        if((pn = findproclist(&cn->pl, p)) == 0) {
          panic("kill: findproclist");
        }
        p->state = RUNNABLE;
        procstatelog(p);
        // remove from channel and push to readylist
        removeproclist(&cn->pl, pn);
        freeproclistnode(pn);
        pushreadylist(p);
        release(&cn->lock);
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
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
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
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
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
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

// for mp2
char*
procstate2str(enum procstate state)
{
  switch(state) {
    case USED:     return "new";
    case SLEEPING: return "waiting";
    case RUNNABLE: return "ready";
    case RUNNING:  return "running";
    case UNUSED:   return "exit";
    case ZOMBIE:   return "exit";
    default:       return "unknown";
  }
}

// Log the state of the current process with a given index.
void
proclog(struct proc* p, int tag)
{
  printf("proclog: pid=%d, ticks=%d, tag=%d, state=\"%s\", priority=%d\n",
          p->pid, ticks, tag, procstate2str(p->state), p->priority);
}

// Log a process state change before it happens
void
procstatelog(struct proc *p)
{
  if(!p->statelogenabled) return; // process state logging of this process is disabled
  printf("procstatelog: pid=%d, ticks=%d, state=\"%s\", priority=%d\n",
          p->pid, ticks, procstate2str(p->state), p->priority);
}

// initialize process list related data structures.
void
proclistinit(void)
{
  int i;
  // initialize proclistnodes.
  for(i = 0; i < NPROCLISTNODE; i++){
    proclistnodes[i].used = 0;
    initlock(&proclistnodes[i].lock, "proclistnode");
  }

  // initialize readylist.
  // initproclist(&readylist);

  // 初始化三個 queue
  initproclist(&l3_queue);
  initsortedproclist(&l2_queue, l2_cmp);  // 比較函數先傳 0 後續改為 cmp function
  initsortedproclist(&l1_queue, 0);  // 比較函數先傳 0

  // initialize channels.
  for(i = 0; i < NCHANNEL; i++){
    channels[i].used = 0;
    initproclist(&channels[i].pl);
    initlock(&channels[i].lock, "channel");
  }
}

// allocate a proclistnode and return it.
// NPROCLISTNODE 是 256
struct proclistnode*
allocproclistnode(struct proc *p)
{
  int i;
  struct proclistnode *pn;
  pn = 0;
  for(i = 0; i < NPROCLISTNODE && pn == 0; i++){
    acquire(&proclistnodes[i].lock);
    if(proclistnodes[i].used == 0){
      proclistnodes[i].used = 1;
      // 這個節點裡面徂的進程
      proclistnodes[i].p = p;
      proclistnodes[i].next = 0;
      proclistnodes[i].prev = 0;
      // 指向節點的指標
      pn = &proclistnodes[i];
    }
    release(&proclistnodes[i].lock);
  }
  return pn;
}

// free a proclistnode.
void
freeproclistnode(struct proclistnode *pn)
{
  acquire(&pn->lock);
  pn->used = 0;
  release(&pn->lock);
}

// initialize a proclist.
void
initproclist(struct proclist *pl)
{
  int i;
  pl->size = 0;
  for(i = 0; i < 2; i++){
    pl->buf[i].used = 1;
    pl->buf[i].p = 0;
    initlock(&pl->buf[i].lock, "proclistsentinel");
  }
  pl->head = &pl->buf[0];
  pl->tail = &pl->buf[1];
  pl->head->next = pl->tail;
  pl->head->prev = 0;
  pl->tail->next = 0;
  pl->tail->prev = pl->head;
  initlock(&pl->lock, "proclist");
}

// get the size of a proclist.
int
sizeproclist(struct proclist *pl)
{
  int size;
  acquire(&pl->lock);
  size = pl->size;
  release(&pl->lock);
  return size;
}

// find a proclistnode in a proclist.
struct proclistnode*
findproclist(struct proclist *pl, struct proc *p)
{
  struct proclistnode *tmp, *pn;
  acquire(&pl->lock);
  pn = 0;
  for(tmp = pl->head->next; tmp != pl->tail && pn == 0; tmp = tmp->next){
    if(tmp->p == p){
      pn = tmp;
    }
  }
  release(&pl->lock);
  return pn;
}

// remove a proclistnode from a proclist.
void
removeproclist(struct proclist *pl, struct proclistnode *pn)
{
  acquire(&pl->lock);
  pl->size--;
  pn->prev->next = pn->next;
  pn->next->prev = pn->prev;
  release(&pl->lock);
}

// pop and return the first element of a proclist, or 0 if the proclist is empty.
struct proclistnode*
popfrontproclist(struct proclist *pl)
{
  struct proclistnode *pn;
  acquire(&pl->lock);
  if(pl->size == 0){
    release(&pl->lock);
    return 0;
  }
  pl->size--;
  pn = pl->head->next;
  pl->head->next = pn->next;
  pn->next->prev = pl->head;
  release(&pl->lock);
  return pn;
}

// push an element to the front of a proclist.
void
pushfrontproclist(struct proclist *pl, struct proclistnode *pn)
{
  acquire(&pl->lock);
  pl->size++;
  pn->next = pl->head->next;
  pn->prev = pl->head;
  pl->head->next->prev = pn;
  pl->head->next = pn;
  release(&pl->lock);
}

// pop and return the last element of a proclist, or 0 if the proclist is empty.
struct proclistnode*
popbackproclist(struct proclist *pl)
{
  struct proclistnode *pn;
  acquire(&pl->lock);
  if(pl->size == 0){
    release(&pl->lock);
    return 0;
  }
  pl->size--;
  pn = pl->tail->prev;
  pl->tail->prev = pn->prev;
  pn->prev->next = pl->tail;
  release(&pl->lock);
  return pn;
}


// push an element to the back of a proclist.
void
pushbackproclist(struct proclist *pl, struct proclistnode *pn)
{
  acquire(&pl->lock);
  pl->size++;
  pn->next = pl->tail;
  pn->prev = pl->tail->prev;
  pl->tail->prev->next = pn;
  pl->tail->prev = pn;
  release(&pl->lock);
}

// initialize a sortedproclist.
void
initsortedproclist(struct sortedproclist *pl, int (*cmp)(struct proc *, struct proc *))
{
  int i;
  pl->size = 0;
  for(i = 0; i < 2; i++){
    pl->buf[i].used = 1;
    pl->buf[i].p = 0;
    initlock(&pl->buf[i].lock, "sortedproclistsentinel");
  }
  pl->head = &pl->buf[0];
  pl->tail = &pl->buf[1];
  pl->head->next = pl->tail;
  pl->head->prev = 0;
  pl->tail->next = 0;
  pl->tail->prev = pl->head;
  pl->cmp = cmp;
  initlock(&pl->lock, "sortedproclist");
}

// get the size of a sortedproclist.
int
sizesortedproclist(struct sortedproclist *spl)
{
  int size;
  acquire(&spl->lock);
  size = spl->size;
  release(&spl->lock);
  return size;
}

// pop and return the first element of a sortedproclist
// following the comparison function, or 0 if the sortedproclist is empty.
struct proclistnode*
popsortedproclist(struct sortedproclist *pl)
{
  struct proclistnode *pn;
  acquire(&pl->lock);
  if(pl->size == 0){
    release(&pl->lock);
    return 0;
  }
  pl->size--;
  pn = pl->head->next;
  pl->head->next = pn->next;
  pn->next->prev = pl->head;
  release(&pl->lock);
  return pn;
}

// push an element to a sortedproclist following the comparison function.
void
pushsortedproclist(struct sortedproclist *pl, struct proclistnode *pn)
{
  struct proclistnode *pn1;
  acquire(&pl->lock);
  pl->size++;
  for(pn1 = pl->head->next; pn1 != pl->tail; pn1 = pn1->next){
    if(pl->cmp(pn->p, pn1->p) > 0){
      break;
    }
  }
  pn->next = pn1;
  pn->prev = pn1->prev;
  pn1->prev->next = pn;
  pn1->prev = pn;
  release(&pl->lock);
}

// compare a process with the first element of a sortedproclist
int
cmptopsortedproclist(struct sortedproclist *spl, struct proc *p)
{
  struct proclistnode *pn;
  int ret;
  acquire(&spl->lock);
  if(spl->size == 0){
    release(&spl->lock);
    return 1; // empty list, return 1 to indicate that p is greater than the first element
  }
  pn = spl->head->next;
  ret = spl->cmp(p, pn->p);
  release(&spl->lock);
  return ret;
}

// allocate a channel, lock and return if available,
// or return 0 if no entry is left.
struct channel*
allocchannel(void *chan)
{
  int i;
  struct channel *cn;
  cn = 0;
  for(i = 0; i < NCHANNEL && cn == 0; i++){
    acquire(&channels[i].lock);
    if(channels[i].used == 0){
      channels[i].used = 1;
      channels[i].chan = chan;
      cn = &channels[i];
    }else{
      release(&channels[i].lock);
    }
  }
  return cn;
}

// find a channel, lock and return if found,
// or return 0 if not found.
struct channel*
findchannel(void *chan)
{
  int i;
  struct channel *cn;
  cn = 0;
  for(i = 0; i < NCHANNEL && cn == 0; i++){
    acquire(&channels[i].lock);
    if(channels[i].used == 1 && channels[i].chan == chan){
      cn = &channels[i];
    }else{
      release(&channels[i].lock);
    }
  }
  return cn;
}

// scheduler managed, push to ready list
int
pushreadylist(struct proc *p)
{
  struct proclistnode *pn;
  // 創建一個節點來包裝 process
  if((pn = allocproclistnode(p)) == 0) {
    panic("pushreadylist: allocproclistnode");
  }
  // implementation step 1
  // 進入 ready queue 時初始化等待時間
  p->wait_ticks = 0;

  // 根據 priority 分配到對應的 queue
  if(p->priority >= 100 && p->priority <= 149) {
    // L1 queue (先不管細節)
    pushsortedproclist(&l1_queue, pn);
  }
  else if(p->priority >= 50 && p->priority <= 99) {
    // L2 queue (先不管細節)
    pushsortedproclist(&l2_queue, pn);
  }
  else if(p->priority >= 0 && p->priority <= 49) {
    // L3 queue - Round Robin,放到尾端
    pushbackproclist(&l3_queue, pn);
  }
  else {
    panic("pushreadylist: priority out of range");
  }

  return 0;
}

// scheduler managed, pop from ready list
struct proc*
popreadylist()
{
  struct proc *p;
  struct proclistnode *pn;
  // 優先從 L1 取 (現在先不管)
  if((pn = popsortedproclist(&l1_queue)) != 0) {
    p = pn->p;
    freeproclistnode(pn);
    return p;
  }

  // 再從 L2 取 (現在先不管)
  if((pn = popsortedproclist(&l2_queue)) != 0) {
    p = pn->p;
    freeproclistnode(pn);
    return p;
  }

  // 最後從 L3 取 - Round Robin 從頭取
  if((pn = popfrontproclist(&l3_queue)) != 0) {
    p = pn->p;
    freeproclistnode(pn);
    return p;
  }
  return 0;
}


// implementation step2
// L2 cmp: priority 高的優先,相同則 pid 小的優先
int 
l2_cmp(struct proc *p1, struct proc *p2)
{
  // Priority 大的優先
  if(p1->priority > p2->priority) {
    return 1;  // p1 優先
  }
  if(p1->priority < p2->priority) {
    return -1;  // p2 優先
  }

  // Priority 相同,pid 小的優先
  if(p1->pid < p2->pid) {
    return 1;  // p1 優先 (pid 小)
  }
  if(p1->pid > p2->pid) {
    return -1;  // p2 優先 (pid 小)
  }

  return 0;  // 完全相同，但不應該發生
}