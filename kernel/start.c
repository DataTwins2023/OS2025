#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// a scratch area per CPU for machine-mode timer interrupts.
uint64 timer_scratch[NCPU][5];

// assembly code in kernelvec.S for machine-mode timer interrupt.
extern void timervec();

// entry.S jumps here in machine mode on stack0.
void
start()
{
  // set M Previous Privilege mode to Supervisor, for mret.
  // 讀取 mstatus 暫存器
  unsigned long x = r_mstatus();
  // 設定 MPP（Machine Previous Privilege）欄位為 S-mode
  // MPP 是 mstatus 暫存器裡的一個欄位（field），佔 2 個 bit，用來記錄「在進入 M-mode 之前，CPU 處於哪個特權模式」。
  // 當發生中斷或例外時，CPU 會跳到 M-mode 處理。處理完畢後，CPU 需要返回到原本的模式繼續執行。MPP 就是用來記住要返回到哪個模式。
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  // 把 main() 函數的地址寫入 mepc 暫存器
  // 告訴 CPU「當我執行 mret 指令時，請跳轉到 main() 函數」
  w_mepc((uint64)main);

  // disable paging for now.
  // 暫時關閉虛擬記憶體的分頁（paging）
  // 因為現在還沒設定好分頁表，要等到後面 main() 裡面才會設定
  w_satp(0);

  // delegate all interrupts and exceptions to supervisor mode.
  // medeleg：委派例外（exception）處理權給 S-mode
  // mideleg：委派中斷（interrupt）處理權給 S-mode
  // sie：啟用 S-mode 的中斷
  // 以後的中斷和例外，讓 S-mode 自己處理，不要每次都回到 M-mode
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // configure Physical Memory Protection to give supervisor mode
  // access to all of physical memory.
  // 允許 S-mode 存取所有實體記憶體
  // S-mode 預設沒有存取記憶體的權限，需要 M-mode 明確授權
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // ask for clock interrupts.
  timerinit();

  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid();
  w_tp(id);

  // switch to supervisor mode and jump to main().
  asm volatile("mret");
}

// arrange to receive timer interrupts.
// they will arrive in machine mode at
// at timervec in kernelvec.S,
// which turns them into software interrupts for
// devintr() in trap.c.
void
timerinit()
{
  // each CPU has a separate source of timer interrupts.
  // 取得當前 CPU 的 ID（多核心中，每個 CPU 的計時器是獨立的）
  int id = r_mhartid();

  // ask the CLINT for a timer interrupt.
  // 透過 CLINT_MTIME 取得現在的時間並加上一個 Interval 來設定第一次中斷的時間
  int interval = 10000; // cycles; about 1/10th second in qemu.
  // CLINT_MTIMECMP 是一個巨集，用來計算每個 CPU 的時鐘比較暫存器的記憶體地址
  *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;

  // prepare information in scratch[] for timervec.
  // scratch[0..2] : space for timervec to save registers.
  // scratch[3] : address of CLINT MTIMECMP register.
  // scratch[4] : desired interval (in cycles) between timer interrupts.
  uint64 *scratch = &timer_scratch[id][0];
  scratch[3] = CLINT_MTIMECMP(id);
  scratch[4] = interval;
  // 把 scratch 地址寫到 mscratch 暫存器，這樣後續才能找到這塊記憶體
  w_mscratch((uint64)scratch);

  // set the machine-mode trap handler.
  // 這個設定一直有效
  // 透過 m_tvec 設定中斷處理函數 timervec，中斷發生時會跳到這裡執行。因為 xv6 中，M-mode 只處理時鐘中斷，所以可以這樣設定
  w_mtvec((uint64)timervec);

  // enable machine-mode interrupts.
  // 啟用 M-mode 中斷，透過讀取 mstatus 設定 MIE（machine interrupt enable)
  w_mstatus(r_mstatus() | MSTATUS_MIE);

  // enable machine-mode timer interrupts.
  // 啟用時間中斷，透過 MIE 啟用時鐘中斷
  w_mie(r_mie() | MIE_MTIE);
}
