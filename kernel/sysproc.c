#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_priorfork(void)
{
  // TODO
  int priority, statelogenabled;
  argint(0, &priority);
  argint(1, &statelogenabled);
  return priorfork(priority, statelogenabled);
}

uint64
sys_proclog(void)
{
  // TODO
  struct proc *p = myproc();
  int tag;
  argint(0, &tag);
  acquire(&p->lock);
  proclog(p, tag);
  release(&p->lock);
  return 0;
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  // 從 user space 拿到參數（要睡眠多少個 ticks）
  argint(0, &n);
  if(n < 0)
    n = 0;
  // 拿到鎖
  acquire(&tickslock);
  // 紀錄開始時間
  ticks0 = ticks;
  // 當還沒睡夠時間，且行程沒被殺死
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    // 進入睡眠
    // 在 ticks 這個 channel 睡眠
    // 等待 wakeup(&ticks) 喚醒
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
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
