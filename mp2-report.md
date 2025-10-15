# MP2_report_38

## Team Member & Contributions

 * ## **資應碩二 113065530 許恩嘉**
 
 * ## **資工碩二 113062636 吳征彥**



## Trace Code

### 1-1. Setup timer interrupt
這可以從系統啟動會執行的 kernel/start.c 開始說明。
在系統一開始啟動時，透過 kernel/kernel.ld 告訴 CPU 要從 `_entry` 符號開始執行

```c
OUTPUT_ARCH( "riscv" )
// 從 _entry 符號開始執行
ENTRY( _entry )

SECTIONS
{
  ...
}
```

而 _entry 定義在 kernel/entry.S 中，它會做一系列動作然後 call `start()` 函數
```c
...
.global _entry
_entry:
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + (hartid * 4096)
        la sp, stack0
        li a0, 1024*4
        csrr a1, mhartid
        addi a1, a1, 1
        mul a0, a0, a1
        add sp, sp, a0
        # jump to start() in start.c
        call start
...
```

start 最重要的任務就是做一些設定後把 CPU 從 M-mode 切換到 S-mode 然後跳轉到 main function
包含：
1. 透過 MPP 設定執行完中斷或例外後，要返回的模式，這邊是 S-mode
```c
// 讀取 mstatus 暫存器
unsigned long x = r_mstatus();
// 設定 MPP（Machine Previous Privilege）欄位為 S-mode
// MPP 是 mstatus 暫存器裡的一個欄位（field），佔 2 個 bit，用來記錄「在進入 M-mode 之前，CPU 處於哪個特權模式」。
// 當發生中斷或例外時，CPU 會跳到 M-mode 處理。處理完畢後，CPU 需要返回到原本的模式繼續執行。MPP 就是用來記住要返回到哪個模式。
x &= ~MSTATUS_MPP_MASK;
x |= MSTATUS_MPP_S;
w_mstatus(x);
```
2. 透過 mepc 設定返回地址為 main function
```c
w_mepc((uint64)main);
```
3. 關閉分頁機制，因為現在還沒有設定好分頁表，要等到 main() 才會設定
```c
w_satp(0);
```
4. 把中斷以及例外的處理權交給 S-mode 並開啟 S-mode 的中斷
```c
// medeleg：委派例外（exception）處理權給 S-mode
// mideleg：委派中斷（interrupt）處理權給 S-mode
// sie：啟用 S-mode 的中斷
w_medeleg(0xffff);
w_mideleg(0xffff);
w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
```
5. 允許 S-mode 存取全部的實體記憶體
```c
// 允許 S-mode 存取所有實體記憶體
// S-mode 預設沒有存取記憶體的權限，需要 M-mode 明確授權
w_pmpaddr0(0x3fffffffffffffull);
w_pmpcfg0(0xf);
```
6. timerinit 初始化計時器，讓系統可以定期產生時鐘中斷，過程如下

    a. 取得當前 CPU 的 ID（多核心中，每個 CPU 的計時器是獨立的）
    ```c
    int id = r_mhartid();
    ```
    b. 透過 CLINT_MTIME 取得現在的時間並加上一個 Interval 來設定第一次中斷的時間
        - CLINT_MTIMECMP 是一個巨集，用來計算每個 CPU 的時鐘比較暫存器的記憶體地址
    ```c
    // ask the CLINT for a timer interrupt.
    int interval = 10000; // cycles; about 1/10th second in qemu.
    *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;
    ```
    c. 準備 scratch 給 中斷處理函數 timervec 使用， scratch 結構

    - scratch[0..2]：用於保存暫存器（a1, a2, a3）

    - scratch[3]：儲存 CLINT_MTIMECMP 的地址

    - scratch[4]：儲存 interval（中斷間隔時間）
    ```c
    uint64 *scratch = &timer_scratch[id][0];
    scratch[3] = CLINT_MTIMECMP(id);
    scratch[4] = interval;
    ```
    d. 把 scratch 地址寫到 mscratch 暫存器，這樣後續才能找到這塊記憶體
    ```c
    w_mscratch((uint64)scratch);
    ```
    e. 透過 w_mtvec 設定中斷處理函數 timervec，中斷發生時會跳到這裡執行。
因為 xv6 中，M-mode 只處理時鐘中斷，所以可以這樣設定  
    ```c
    w_mtvec((uint64)timervec);
    ``` 
    - 這邊有一個問題 ”一開始不是就有委派中斷給 S-mode 了嗎 這樣為什麼還會有中斷在 m-mode？“
    
        時鐘中斷可以分為
        
        Ⅰ. Machine Timer Interrupt
        -
        - 進入 M-mode 
        - 由 CLINT 硬體產生 
        - xv6 使用這個
        - 雖然看起來可以委派，但硬體會忽略

        Ⅱ. Supervisor Timer Interrupt
        -
        - 進入 S-mode 
        - 可以由軟體或其他硬體產生 
        - xv6 沒有直接使用
        
        所以需要設定 mtvec register
    - kernel/kernelvec.S 的 timervec 在做什麼

        Ⅰ. 使用 scratch 做
        -
        - 保存暫存器
        ```c
        csrrw a0, mscratch, a0
        sd a1, 0(a0)
        sd a2, 8(a0)
        sd a3, 16(a0)
        ```
        - 設定下一次時鐘中斷的時間
        ```c
        ld a1, 24(a0) # CLINT_MTIMECMP(hart)
        ld a2, 32(a0) # interval
        ld a3, 0(a1)
        add a3, a3, a2
        sd a3, 0(a1)
        ```
        - 設定 S-mode 軟體中斷，這邊就跳回 MP1 裡面會看到的 yield()
        ```c
        li a1, 2
        csrw sip, a1
        ```
        - 恢復暫存器
        ```c
        ld a3, 16(a0)
        ld a2, 8(a0)
        ld a1, 0(a0)
        csrrw a0, mscratch, a0
        ```
        Ⅱ. 透過 mret 把模式恢復到 mstatus.MPP 指定的模式（S-mode），並且 PC 跳回 mepc 指定的地址
        -
        返回後：
        - 程式繼續執行，但 S-mode 軟體中斷已經設定好了
        - 很快就會再次中斷，進入 S-mode 處理
    
    f. 啟用 M-mode 中斷，透過讀取 mstatus 設定 MIE（machine interrupt enable)
    ```c
    // 啟用 M-mode 中斷，透過讀取 mstatus 設定 MIE（machine interrupt enable)
    w_mstatus(r_mstatus() | MSTATUS_MIE);
    ```
    g. 啟用時間中斷，透過 MIE 啟用時鐘中斷
    ```c
    // 啟用時間中斷，透過 MIE 啟用時鐘中斷
    w_mie(r_mie() | MIE_MTIE);
    ```
7. 紀錄 CPU 編號到 tp register

    a. r_mhartid() 只能在 M-mode 使用
    
    b. 在多核心系統中，所有 CPU 都會執行 start() 函數
    
    c. 這樣每個 CPU 都會把自己的 ID 存到 tp
    ```c
    int id = r_mhartid();
    w_tp(id);
    ```
8. 透過 mret 指令回到 S-mode 並呼叫 main function
```c
asm volatile("mret");
```



### 1-2. User space interrupt handler
當在 user space 時發生 timer interrupt
會依據 kernel/start.c 中 `timerinit()` 的
```c
w_mtvec((uint64)timervec)
```
跳到 kernel/kernelvec.s 中的 `timervec`，其中透過
```c
# arrange for a supervisor software interrupt
# after this handler returns.
li a1, 2
csrw sip, a1
```
設置 sip 中的 SSIP bit 標記一個 S-mode 軟體中斷待處理
之後透過 mret 回到之前的模式（user space）
CPU 檢測到 sip 有 pending interrupt
這個 S-mode 的軟體中斷會因為 kernel/trap.c 中的 `usertrapret()`
```c
w_stvec(trampoline_uservec);
```
跳到 kernel/trampoline.S 的 `uservec` 段落執行，在這個段落中
```c
# jump to usertrap(), which does not return
jr t0
```
會跳到 kernel/trap.c/usertrap()

過程中會透過 `devintr()` 知道這是哪一個設備發出的 interrupt 並放到 which_dev

kernel/trap.c devintr() 在做
-  讀取中斷原因
-  第一種是外部裝置中斷
    - 透過 plic 知道是哪個裝置中斷
    - 依據裝置呼叫對應的處理函數
    - 回傳 1 表示裝置中斷
-  第二種是軟體中斷（從 M-mode 來的），也就是我們要的
    - 處理實際的 timer interrupt 處理 - kernel/trap.c clockintr()（只讓 cpuid 0 的處理器處理）
        - 取得鎖
        - 更新全域時間 ticks++
        - wake up 某個 channel 所有等待時間到的 process
        - 會先檢查，不能 wakeup 自己，被 wakeup 的 process 狀態要是 sleeping 且屬於該 channel
        - 把狀態改為 RUNNABLE 並加入對應的 ready queue（按照 priority）
        - 釋放鎖
    - 清除軟體中斷的 pending bit
    - 回傳 2 表示 timer intrrupt
-  第三種是不認識的中斷
    - 回傳 0 表示不認識的中斷

接著就是上次 mp1 的過程，要注意的是到 line 80 時
```c
// give up the CPU if this is a timer interrupt.
if(which_dev == 2)
implicityield();
```
如果發現是 timer 產生的 interrupt 就會做 `implicityield()`，這是在 kernel/proc.c 中
```c
// Implicit yield is called on timer interrupt
void
implicityield(void)
{
  struct proc *p = myproc();
  if(ticks - p->startrunningticks >= 1) {
    // yield round robin scheduling
    // actually ticks - p->startrunningticks should be 1
    yield();
  }
}
```
會去計算這個 process 執行多久，如果大於 1 個 tick，就要讓出 CPU（呼叫 kernel/proc.c yield()）
```c
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
```
目前 process 的狀態從 RUNNING -> RUNNABLE，放回 ready queue 並呼叫 `sched()` 切換回 scheduler context，Scheduler 會選擇下一個進程執行



### 1-3. Kernel space interrupt handler
在 kernel/trap.c `usertrap()` 中會先把 stvec 設置為 kernel/kernelvec.S kenrelvec 的位址（因為已經進入 kernel mode，這時候如果遇到 trap 應該要跳到 kernel/trap.c 中的 kernelvec 而非 trampoline.S 中的uservec）
```c
w_stvec((uint64)kernelvec);
```
所以當 kernel space 中發生 timer interrupt， CPU 會自動跳到 kernel/kernelvec.S kernelvec，並依照
```c
# call the C trap handler in trap.c
call kerneltrap
```
跳到 kerneltrap（在 kernel/trap.c 中）
kerneltrap 一樣會透過 kernel/trap.c 中的 `devintr()` 判斷中斷類型
kernel/trap.c devintr() 在做
-  讀取中斷原因
-  第一種是外部裝置中斷
    - 透過 plic 知道是哪個裝置中斷
    - 依據裝置呼叫對應的處理函數
    - 回傳 1 表示裝置中斷
-  第二種是軟體中斷（從 M-mode 來的），也就是我們要的
    - 處理實際的 timer interrupt 處理 - kernel/trap.c clockintr()（只讓 cpuid 0 的處理器處理）
        - 取得鎖
        - 更新全域時間 ticks++
        - wake up 某個 channel 所有等待時間到的 process
        - 會先檢查，不能 wakeup 自己，被 wakeup 的 process 狀態要是 sleeping 且屬於該 channel
        - 把狀態改為 RUNNABLE 並加入對應的 ready queue（按照 priority）
        - 釋放鎖
    - 清除軟體中斷的 pending bit
    - 回傳 2 表示 timer intrrupt
-  第三種是不認識的中斷
    - 回傳 0 表示不認識的中斷

如果是 timer interrupt 且狀態是 running，則要執行 implicityyield。會去計算這個 process 執行多久，如果大於 1 個 tick，就要讓出 CPU（呼叫 kernel/proc.c `yield()`）

*一個重要觀念： 為什麼 kerneltrap 只需要保存關鍵暫存器在 stack（本地變數） 上， usertrap 卻是用 trapframe 保存所有狀態?*

Ans: 可以想成 usertrap 會觸發 mode 改變，進到 kernel mode 後 user 的暫存器值會被破壞，所以需要完整保存。但 kerneltrap 發生時 mode 不會改變，可以把重要的暫存器直接存在 stack 上

### 2. Mapping relationship
| xv6 | lecture notes | Definition|
| -------- | -------- | -------- |
| `UNUSED`     |  `X`    | This process slot is not in use; it can be allocated for a new process. |
| `USED` | `New` | The slot has been allocated but the process is not yet runnable, e.g., just created. |
| `SLEEPING` | `Waiting` | The process is blocked, waiting for an event to occur. |
| `RUNABLE` | `Ready` | The process is ready to run and waiting for CPU scheduling. |
| `RUNNING` | `Running` | The process is currently executing on the CPU. |
| `ZOMBIE` | `Terminated` | The process has finished execution but has not yet been reaped by its parent. |


### 3. Explain what each functions in the state transition function path
1. `New` -> `Ready`
- userinit -> allocproc -> pushreadylist

    這個路徑描述的是系統啟動時創建第一個 user process 的過程。當系統啟動呼叫完 kernel/start.c 後，透過 mret 會跑到 kernel/main.c main()，其中 line 32 會做 userinit（在 kernel/proc.c 中）

    userinit 做
    - 透過 allocproc 分配進程，把這個進程保存到全域變數 `initproc`（這是系統第一個進程，之後會成為所有「孤兒進程」的養父）
        
        allocproc 做
        - 找到 proc[] 陣列中 state == `UNUSED` 的 slot，如果找不到就返回 0，找到的話就跳去 found；在檢查每個 proc[] 元素時，需要上鎖以防止多個 CPU 同時選到同一個 slot
        - found 中會透過 kernel/proc.c 的 `allocpid` 分配 process ID，並且修改狀態
        - 呼叫 kalloc 分配一個 page 用來存放 trapframe；失敗的話要清理已分配的資源然後釋放鎖
        - 分配一個 pagetabl；失敗的話要清理已分配的資源然後釋放鎖
        - 初始化並清空 context
        - 設定 context.ra ，這邊設為 `forkret` （一個在 Kernel/proc.c 的特殊函數）新進程第一次被調度時會執行它
        - 設定 context.sp 為 kernel stack 的頂端 （因為 context 是給 kernel mode 使用的 context 的 sp = kernel stack，trapframe 才是給 user mode 使用，所以 trapframe 的 sp = user stack）

    - 透握 uvmfirst 載入 initcode 到進程的 virtual addr 0
    - 設定 trapframe 的 epc 跟 sp（user stack）這邊都是虛擬地址
    - 把 p -> name 設為 initcode
    - 把當前目錄設為 root
    - 修改狀態為 `RUNNABLE` 並透過 pushreadylist（在 kernel/proc.c 中） 內使用 allocproclist 創建一個節點來包裝 process，再透過 pushbackproclist 放到 ready list 的尾端
        allocproclist 做
        - 把 process 包裝成一個 node
        - 這樣做的好處是一個 process 可以包裝成多個 node 放在不同的 list ，這樣就可以指向多個 next, prev
        - 因為只是打包成 node ，所以 next 及 prev 都不會在這邊設定，會等到 pushbackproclist 才設定
    - 釋放鎖

- fork or priorfork -> allocproc -> pushreadylist

    和前面的過程比較，發現只差在第一個步驟。前面是系統啟動創建的第一個 process ，後面則是正常透過 fork/ priorfork （都在 kernel/proc.c 中）創建 process

    在 fork 內會做
    - 透過 allocproc 分配新的 process（allocproc 做的事情在上面已經有說明）
    - 設定優先級
    - 透過 uvmcpoy 複製 parent process 的記憶體；失敗的話要清理已分配的資源然後釋放鎖
    - 複製 trapframe 內容
    - 設定 子進程 fork() 返回 0
    - 複製打開的檔案（實際上就是讓父子共享同一個檔案，增加檔案的引用技術）
    - 複製父進程的當前目錄 為 子進程的當前目錄（實際上就是讓父子共享同一個目錄）
    - 複製名稱
    - 設定父子關係
    - 修改狀態為 `RUNNABLE` 並透過 pushreadylist（在 kernel/proc.c 中） 內使用 allocproclist 創建一個節點來包裝 process，再透過 pushbackproclist 放到 ready list 的尾端
2. `Running` -> `Ready`
- kerneltrap, usertrap -> yield -> pushreadylist -> sched -> kernel/switch.S:swtch
3. `Running` -> `Waiting` (Consider the case of sleep system call)
4. `Waiting` -> `Ready`
5. `Running` -> `Terminated`
6. `Ready` -> `Running`