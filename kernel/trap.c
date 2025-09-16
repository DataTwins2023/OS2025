#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
// trap 包含 軟體 exception 以及 硬體 interrupt
void
usertrap(void)
{
  int which_dev = 0;
  // 一個安全檢查
  // 檢察系統調用是否真的來自 user mode
  // 如果不是，則觸發 panic
  // SSTATUS_SPP 是 sstatus 寄存器中的一個位元
  // 當 SPP 為 0 時，表示之前來自 user mode
  // 當 SPP 為 1 時，表示之前來自 supervisor mode
  // r_sstatus() 函數讀取 sstatus 寄存器的值
  // 這裡檢查 SPP 位元是否為 0 (user mode)
  // 如果不是，則觸發 panic，表示有嚴重錯誤
  // 這邊是 身份檢查
  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  // 設置 stvec 寄存器，指向 kernelvec 函數的地址
  // 基本上是在說 user mode 跟 kernel mode 都會遇到 trap
  // 但 user mode 遇到 trap 時會跳到 uservec
  // 而 kernel mode 遇到 trap 時會跳到 kernelvec
  // 這樣可以確保在不同的執行模式下，trap 都能被正確處理
  // w_stvec() 函數用來寫入 stvec 寄存器
  // (uint64)kernelvec 是將 kernelvec 函數的地址轉換為 64 位元整數
  // 這樣 CPU 在發生 kernel mode 中 trap 時會跳轉到 kernelvec
  // kernelvec 會保存 kernel mode 的上下文，然後調用 kerneltrap 函數來處理 trap
  w_stvec((uint64)kernelvec);

  // 取得「發起當前系統調用」的用戶進程的 PCB
  struct proc *p = myproc();

  // save user program counter.
  // r_sepc() 函數讀取 sepc 寄存器的值
  // sepc 寄存器保存了發生 trap 時的用戶程式計數器 (PC)
  // 這樣可以在處理完 trap 後，
  // 將控制權返回到正確的用戶程式位置
  // p->trapframe->epc 是進程的 trapframe 中保存用戶程式計數器的欄位
  // 將 sepc 的值存入 p->trapframe->epc 
  // 為什麼不在 trampoline.S 裡面存呢？
  p->trapframe->epc = r_sepc();
  
  // r_scause() 函數讀取 scause 寄存器的值
  // scause 的值是硬體自動判斷 trap 原因及類型就寫入的，軟體不參與
  // scause 寄存器保存了 trap 的原因
  // 這邊是 原因檢查
  if(r_scause() == 8){
    // system call

    // 檢查進行是否被標記為需要終止
    // 如果是的話，則調用 exit(-1) 終止進程
    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    // 指向 ecall 指令的下一個位址
    // 也就是 user/usys.S 的 ret (row27)
    // ret 會再返回到 user/grep.c
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    // 重新啟用中斷
    // 為什麼重新啟用 是因為 發生 trap 時硬體會自動關閉中斷並跳轉到 stvec 暫存器指向的位址
    // 原本是指向 uservec
    // 現在改成指向 kernelvec
    // 這樣就不會再跳回 uservec
    intr_on();

    // 處理 user mode 發過來的系統調用請求
    syscall();
  } else if((which_dev = devintr()) != 0){ // 檢查是否為設備中斷
    // 設備中斷的處理流程
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    // r_stval() 函數讀取 stval 寄存器的值
    // stval 寄存器保存了與 trap 相關的額外資訊
    // 這些資訊有助於診斷和處理 trap
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();
  // 準備返回 user space
  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  // 因為要將 stvec 從 kerneltrap 切換回 usertrap
  // 所以在切換過程中先關閉中斷
  // 以避免在中斷期間發生不一致的狀態
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  // 這邊會這樣設定是因為 trampoline 需要被所有 user process 共享訪問
  // trampoline.S 在編譯時會有編譯器分配的地址 (如 0x80001000)
  // 但在運行時，為了讓所有用戶進程都能訪問（物理記憶體中只有一段 trampoline 代碼，但映射關係會把他們映射到每一個進程的相同位址），會被重新映射到 TRAMPOLINE 高虛擬地址
  // 所以需要計算 uservec 在 trampoline 頁面中的相對偏移量
  // 然後加上運行時的 TRAMPOLINE 基地址
  // 這樣就能得到 uservec 的正確運行時虛擬地址，確保硬體能正確跳轉
  // uservec 和 trampoline 都定義在 trampoline.S 裡面
  // TRAMPOLINE 是在 memlayout.h 中定義的高虛擬地址常數
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  // 設置 trapframe 中的欄位
  // 這些欄位會在下一次從 user mode 進入 kernel mode 時被 uservec 使用
  // 那第一次進入 kernel mode 時是誰設置的呢？
  // 創建進程時會在 kernel/proc.c 裡面的 allocproc() 設置
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  // 將 SPP 設置為 0 (user mode)
  unsigned long x = r_sstatus();
  // SSTATUS_SPP 定義在 kernel/riscv.h 中
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  // 控制再返回 user mode 後是否啟用中斷
  // intr_on() / intr_off() 是設置 SIE 位元
  // 但這裡不直接設置 SIE
  // SIE：控制「當前模式」的中斷狀態
  // 而是設置 SPIE
  // SPIE：kernel 預設 user mode 的中斷狀態
  // 因為當 之後 userret 執行 sret 指令返回 user mode 時
  // 會將 SPIE 的值複製到 SIE
  // 如果 SPIE 是 1，則返回 user mode 後中斷會被啟用
  // 如果 SPIE 是 0，則返回 user mode 後中斷會被禁用
  // usertrapret() 裡面已經關閉了中斷
  // 所以這裡設置 SPIE 為 1
  // 這樣返回 user mode 後中斷就會被啟用
  // SSTATUS_SPIE 定義在 kernel/riscv.h 中
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  // 設定返回 user mode 後的程式計數器 (PC)
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  // 將用戶頁表地址轉換為 SATP 暫存器的格式
  // 這個值會傳遞給 userret 函數
  // userret 會用這個值切換回用戶頁表
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  // 計算必跳到 userret 函數的位址（帶上用戶頁表）
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

