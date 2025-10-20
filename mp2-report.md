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

如果是 timer interrupt 且 process 狀態是 running，則要執行 implicityyield。會去計算這個 process 執行多久，如果大於 1 個 tick，就要讓出 CPU（呼叫 kernel/proc.c `yield()`）

*問題： 為什麼 kerneltrap 中的implicityield 要判斷 process -> state 但 usertrap 卻不需要？*

Ans: 因為 usertrap 發生時，process 的 state 必定是 running；但 kerneltrap 發生在 kernel space，這時可能正在執行 scheduler，它是核心的獨立控制流，因此 myproc() = 0

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
    - 透過 allocproc 分配進程，把這個 process 保存到全域變數 `initproc`（這是系統第一個進程，之後會成為所有「孤兒進程」的養父）
        
        allocproc 做
        - 找到 proc[] 陣列中 state == `UNUSED` 的 slot，如果找不到就返回 0，找到的話就跳去 found；在檢查每個 proc[] 元素時，需要上鎖以防止多個 CPU 同時選到同一個 slot
        - found 中會透過 kernel/proc.c 的 `allocpid` 分配 process ID，並且修改狀態
        - 呼叫 kalloc 分配一個 page 用來存放 trapframe；失敗的話要清理已分配的資源然後釋放鎖
        - 分配一個 pagetable；失敗的話要清理已分配的資源然後釋放鎖
        - 初始化並清空 context
        - 設定 context.ra ，這邊設為 `forkret` （一個在 Kernel/proc.c 的特殊函數）新 process 第一次被調度時會執行它
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

        pushbackproclist 就是把打包好的 node 加入 proclist 這個 Doubly Circular Linked List 的尾端
    - 釋放鎖，這個鎖應該是從 allocproc 得到的

- fork or priorfork -> allocproc -> pushreadylist

    和前面的過程比較，發現只差在第一個步驟。前面是系統啟動創建的第一個 process ，後面則是正常透過 fork/ priorfork （都在 kernel/proc.c 中）創建 process

    在 fork 內會做
    - 透過 allocproc 分配新的 process（allocproc 做的事情在上面已經有說明）
    - 設定優先級
    - 透過 uvmcpoy 複製 parent process 的記憶體；失敗的話要清理已分配的資源然後釋放鎖
    - 複製 trapframe 內容
    - 設定 子進程 fork() 返回 0
    - 複製打開的檔案（實際上就是讓父子共享同一個檔案，增加檔案的引用計數）
        - 這邊的重要概念是，透過引用計數，可以避免 dangling pointer。 如果直接讓 child process 透過一個指標指向 parent process 本來就指向的檔案，但如果 child close 並釋放記憶體，那 parent 指向該檔案的指標就變成 dangling 了，所以透過計數可以讓檔案資源只有在計數為 0 時釋放
    - 複製父進程的當前目錄 為 子進程的當前目錄（實際上就是讓父子共享同一個目錄）
    - 複製名稱
    - 設定父子關係
    - 修改狀態為 `RUNNABLE` 並透過 pushreadylist（在 kernel/proc.c 中） 內使用 allocproclist 創建一個節點來包裝 process，再透過 pushbackproclist 放到 ready list 的尾端

    priorityfork 只是可以設定優先級和 log 的方式
2. `Running` -> `Ready`
- kerneltrap, usertrap -> yield -> pushreadylist -> sched -> kernel/switch.S:swtch

    `Running` -> `Ready` 這個狀態轉換的原因是因為某個正在被執行的 process 需要讓出 CPU 時,
    主要原因包括:
    - Timer interrupt (時間片用完)
    - 被高優先級 process preempt (搶佔)
    - 主動呼叫 yield

    Process 會被移回 Ready Queue 等待下次被 scheduler 選中。所以這邊的說明可以直接從 usertrap/ kerneltrap 開始。

    usertrap: 當 process 在 user space 執行時發生 trap 會執行的處理程序

    kerneltrap: 當 process 在 kernel space 執行時發生 trap 會執行的處理程序

    雖然這兩個處理程序（都在 kernel/trap.c 中）觸發的條件不同，內部的動作也不盡相同，但它們有著相同的主要行為 ——  __透過 `devintr()` 檢查觸發程序的原因__
    
    如果發現是 timer interrupt（`devintr()` 回傳 2）（ kerneltrap 會多檢查 `myproc()` 以及 `myproc() -> state`），就會執行 `implicityield()`

    `implicityield()` 檢查 process 是不是執行超過 1 個 ticks 如果是的就執行 `yield()`

    `yield()` 中會做
    - 取得 process 的鎖
    - 將 process 狀態改為 `RUNNABLE`（`READY`）
    - 呼叫 `pushreadylist()` 放入 ready queue
    - 呼叫 `sched`（在 kernel/proc.c 中） 會做
        - 確認是否取得鎖，以及只有一個鎖
        - 確認狀態是否有修改成功
        - 確認中斷已關閉，因為上下文切換期間不能被中斷
        - 透過 intena 保存當前形成的中斷啟用狀態
        - 透過 swtch （定義在 kernel/swtch.S）儲存當前行程的上下文 並切換到 CPU 的 scheduler context（在 kernel/proc.h 的 struct cpu 中），使 scheduler 能繼續執行以選擇下一個可執行的行程， `swtch` 做
            - 保存舊的 context（參數 1）
            - 載入新的 context (參數 2)
            - 返回新的 ra (也就是 scheduler 中 swtch 呼叫下一行的位址)
                - scheduler 會做（但現在的 kernel/proc.c `scheduler` 好像少了 swtch 這一段）
                    -  最重要的事情就是 swtch （這是在 sched 中的），假設現在有一個 process A 執行後要做 `yield`，就會做 `swtch` ，這時會：
                        1. 把當下 process A 的暫存器 `ra` （包括 `ra`、`sp`、`s0-s11`）存到該 process 的 context

                        2. 把 cpu -> context 的內容重新載入暫存器（其中就包括 scheduler 之前呼叫 swtch(&c->context, &p->context) 時的下一行位址，這個位址會被放在 `ra`）

                        3. 接著透過 `ret` 回到 scheduler 中

                    - 等到 scheduler 重新挑選一個要被執行的 process （ process B ）又會再做一次 `swtch` （schdeuler 中的）

                        1. 把 process B 的 context 放到暫存器中（包含之前保存 `ra`）

                        2. 把 scheduler 的暫存器值存到 cpu 的 context 中
                        
                        3. 接著執行 `ret` 就可以順利切換到 process B
        - 當 process 再次被 scheduler 選中，會回到 mycpu()->intena = intena; 恢復 intena
    - 釋放 process 的鎖
3. `Running` -> `Waiting` (Consider the case of sleep system call)
- sys_sleep -> sleep -> sched

    這個狀態的轉換是當某個 process 需要等待另一個事件（例如 I/O complete）時所發生的，可以直接從 `sys_sleep` （在 kernel/sysproc.c 中）出發

    在 `sys_sleep` 中會
    - 從 user space 拿到參數（要睡眠多少個 ticks）
    - 拿到 tickslock 鎖
    - 紀錄開始時間
    - while 迴圈在還沒到達睡眠時間且 process 沒被殺死時
        - 會持續 `sleep(&ticks, &tickslock)` （在 kernel/proc.c 中）在 ticks channel 中睡眠，等待 `wakeup(&ticks)` 喚醒
        - `sleep` 會
            - 取得 prcoess 鎖，釋放 sleep 參數中傳入的鎖
            - 找到或創建一個 channel 結構來管理等待這個通道的 process
            - 記錄在哪個 channel 睡眠並改變狀態（`RUNNING` -> `SLEEPING`）
            - 把 process 透過 `allocproclistnode` 包裝成 node 並加入 channel 尾端
            - 呼叫 `sched()`，這時候 ra 是下一行的 p -> chan = 0; 當 process 重新取得 CPU 會從這邊開始執行
                - 進入 `sched()` 後會切換到 scheduler 選擇下一個要執行的 process
                - 之後回到這個 process 就會執行 p -> chan = 0
            - p -> chan = 0 是讓這個 process 不再屬於該 channel
    - 解開 tickslock 鎖
4. `Waiting` -> `Ready`  
- `clockintr` -> `wakeup`

    狀態轉換的時機是，當 process 等待的事件發生時 (例如: sleep 時間到、I/O 完成等)

    但這邊提到的 `clockintr` 是在 timer interrupt 情況下會觸發的 funtcion
    
    `clockintr` 中會
    - 增加 ticks
    - `wakeup(&ticks)` （實作在 kernel/proc.c）
        - &ticks 是全域變數 ticks 的記憶體位址，在這邊的用法是透過「記憶體位址」來標識「等待的是哪個事件」，也把它視為一個 channel
        - wakeup 就會去喚醒所有在等待這個事件的 process
        - wakeup 中會做
            - 利用傳入的*chan參數(這裡是&ticks)透過`findchannel(chan)`找到對應的 channel ，如果沒找到則直接 return
            - `while((pn = popfrontproclist(&cn->pl)) != 0)` 迴圈會從佇列前端彈出一個 node 直到空了
            - 在`while`迴圈中
                - `p = pn->p`: 取得 node 的process
                - `freeproclistnode(pn)`: 釋放 node 的記憶體
                - 在喚醒這個process之前先檢查喚醒是否合法，其中包含: 
                    - running proc 不能 wakeup 自己
                    - 只能 wakeup 處於 `SLEEPING` 狀態的 process
                    - 要 wakeup 的 process 處於正確的 channel
            - 上面三個檢查通過後即更新 p->state = RUNNABLE
            - `procstatelog(p);` : 紀錄程序轉換的日誌
            - `pushreadylist(p);` : 將此process push到ready queue，後續等待schedular安排是否要讓它`Running`
            - 最後釋放這個 process 的lock，即完成這個 process 的喚醒
            - 當channel全部的節點的processes都被喚醒後做 `cn->used = 0;` 標記這個 channel 沒在使用
        - 這邊有一點需要注意，如果每次都會 wakeup 全部等待 ticks 事件的 process ，那就代表不管 process 要 sleep 幾秒，都會被 wakup 變成 `RUNNABLE`，但後續 `sys_sleep` 中有透過其他機制強迫還沒到等待時間的 process 繼續 sleep 

        
5. `Running` -> `Terminated`
- sys_exit -> exit -> sched
   - `sys_exit`(kernel/sysproc.c): 當 running 的 user process 要 terminate 時呼叫的 system call 
    ```c
    uint64
    sys_exit(void)
    {
    int n;
    argint(0, &n);
    exit(n); // 在這邊呼叫 kernel 的exit(n) 並把從 user space 取得的參數傳入 
    return 0;  // not reached
    }
    ```
    - `exit`(kernel/proc.c): 為 kernel 中處理 running process 要 terminate 的程式碼
    ```c
    struct proc *p = myproc(); //首先取得正在 running 的 process
    // 一開始先檢查目前正在跑的 process 是否為初始程序(initproc) 
    // 初始程序不能 exit 否則會產生孤兒程序無法被妥善處理
    if(p == initproc)
        panic("init exiting");
    ```
    - 接著處理預 terminate process 的檔案資源釋放，包含:  
      - 關閉所有相關的 open files
      - 將 process CWD 的 inode reference count 減一 
    - 處理與其 child processes 以及 parent process 的關係，包含:  
      - `reparent(p);`: 將所有其 child processes 的 parent 變成 initproc  
      - `wakeup(p->parent);`: Parent 可能處於 wait() 的 sleep 喚醒它  
        - ticky 的點是 wakeup 是要喚醒等待某個事件發生的 process，而 parent 就待在「自己的位址」這個 channel 上
    - xstate 是退出狀態碼，讓 parent 知道 child 是「正常結束」還是「出錯結束」
    - 更新此process的狀態
    
    ```c
    p->xstate = status; // 更新exit state 之後會成為return value回傳到user space 
    p->state = ZOMBIE; // 更新process state為 ZOMBIE
    procstatelog(p); //將更新紀錄於日誌中
    ...
    sched(); //跳到scheduler 一般情況下不會再返回
    panic("zombie exit");//若意外返回系統進入panic
    ```
    - `sched`:
        - 確認是否取得鎖，以及只有一個鎖
        - 確認狀態是否有修改成功
        - 確認中斷已關閉，因為上下文切換期間不能被中斷
        - 透過 intena 保存當前形成的中斷啟用狀態
        - 透過 swtch （定義在 kernel/swtch.S）儲存當前行程的上下文 並切換到 CPU 的 scheduler context（在 kernel/proc.h 的 struct cpu 中），使 scheduler 能繼續執行以選擇下一個可執行的行程， `swtch` 做
            - 保存舊的 context（參數 1）
            - 載入新的 context (參數 2)
            - 返回新的 ra (也就是 scheduler 中 swtch 呼叫下一行的位址)
                - scheduler 會做（但現在的 kernel/proc.c `scheduler` 好像少了 swtch 這一段）
                    -  最重要的事情就是 swtch （這是在 sched 中的），假設現在有一個 process A 執行後要做 `yield`，就會做 `swtch` ，這時會：
                        1. 把當下 process A 的暫存器 `ra` （包括 `ra`、`sp`、`s0-s11`）存到該 process 的 context

                        2. 把 cpu -> context 的內容重新載入暫存器（其中就包括 scheduler 之前呼叫 swtch(&c->context, &p->context) 時的下一行位址，這個位址會被放在 `ra`）

                        3. 接著透過 `ret` 回到 scheduler 中

                    - 等到 scheduler 重新挑選一個要被執行的 process （ process B ）又會再做一次 `swtch` （schdeuler 中的）

                        1. 把 process B 的 context 放到暫存器中（包含之前保存 `ra`）

                        2. 把 scheduler 的暫存器值存到 cpu 的 context 中
                        
                        3. 接著執行 `ret` 就可以順利切換到 process B
        - 當 process 再次被 scheduler 選中，會回到 mycpu()->intena = intena; 恢復 intena；如果 process 被改為 `terminated` 的話這邊不可能執行，如果因為 bug 回到這個 process 執行 mycpu()->intena = intena 後，會跳出來
    - 觸發 panic("zombie exit");


6. `Ready` -> `Running`
- `scheduler` -> `kernel/switch.S:swtch` -> `popreadylist` -> `kernel/switch.S:swtch`
    - `scheduler` 會從 ready queue 中選出下一個要執行的 process 並修改他的狀態為 RUNNING，但目前的 scheduler 好像少了 `swtch` 動作



## Implementation

開始實作的部分，這邊將實作的過程分為 6 個階段

1. 新增 struct proc（kernel/proc.h）需要的 attribute，以及宣告三個 Ready Queue
2. 實作 L3
3. 實作 L2
4. 實作 L1
5. 不同 Queue 間的 Preemption
6. Aging
---------------------

1. 新增 struct proc（kernel/proc.h）需要的 attribute，以及宣告三個 Ready Queue
    - 新增 attribute

    | 新增的 attribute | 用途 |
    | -------- | -------- |
    | `t_i` & `T` |  L1 Queue； `t_i-1` 不需要紀錄，因為**更新前**的 `p->t_i` 就是 `t_{i-1}`| 
    | `wait_ticks` | aging 機制紀錄到底等了多少 ticks |
    
    - 宣告三個 ready queue（kernel/proc.c 中）
    ```c
    struct proclist l3_queue;
    struct sortedproclist l2_queue;
    struct sortedproclist l1_queue;
    ```

    - 初始化三個 ready queue
    
        實作在 kernel/proc.c 中的 `proclistinit`，他的目的是初始化所有跟「process 排程」有關的資料結構，做三件事
        
        - 初始化 proclistnodes：這裡面有一個動作是 `initlock`，必且會給每個 lock 同樣的名稱，但這不會有問題，因為名稱只是 debug 用，實際在使用還是依照 mem address
        - 初始化 readylist（包括 l1, l2, l3）
        - 初始化 channels

        初始化三個 readyqueue
        ```c
        // initialize readylist.
        // initproclist(&readylist);
        // initialize channels.
        // implementation step 1
        // 原本只初始化 readylist
        // 但現在要初始化 l1, l2, l3 queue
        initproclist(&l3_queue);

        // cmp 先傳入 0 之後再實作
        initsortedproclist(&l2_queue, 0);
        initsortedproclist(&l3_queue, 0);
        ```
        `initproclist` 也實作在 kernel/proc.c ，它會：
        - 初始化 queue 的大小
        - 設置兩個哨兵，然後給他們鎖
        - 設定 head & tail 指標
        - 設定 head 跟 tail 的連結，一開始 head 跟 tail 間沒有東西
        - 初始化 queue 本身的鎖
        
        `initsortedproclist` 跟 `initproclist` 相比只是多了 `cmp` 函數的設置

    - 初始化新的 proc 欄位
        修改 kernel/proc.c 中的 `allocproc()`
        ```c
        // implementation step 1
        + p -> priority = 149;
        + p -> t_i = 0;
        + p -> T = 0;
        + p -> wait_ticks = 0;
        + p -> time_slice_used = 0;
        ```
        新增這一部分

    | function | 目的 |
    | -------- | -------- |
    | `procinit()` | 初始化 process table | 
    | `allocproc()` | 分配一個 process |
    | `proclistinit()` | 初始化所有 process 排程相關的資料結構 |

    
2. 實作 L3 
    - 目前 `pushreadylist` 是將全部的 `RUNNABLE` process 放到 readylist，並沒有依據 priority 分級，所以現在先修改這一部分
        - `pushreadylist` 實作在 kernel/proc.c 中
        - 透過 if-else 的判斷，依據 p -> priority 決定 `proclistnode` 要放入哪一個 ready queue
            ```c
            // 把節點加到 ready list 的尾端
            // pushbackproclist(&readylist, pn);

            // 要依據 priority 決定要放在哪一個 ready queue
            + if(p->priority >= 100 && p->priority <= 149) {
            +     // L1: priority 100-149
            +     pushsortedproclist(&l1_queue, pn);
            + }
            + else if(p->priority >= 50 && p->priority <= 99) {
            +     // L2: priority 50-99
            +     pushsortedproclist(&l2_queue, pn);
            + }
            + else if(p->priority >= 0 && p->priority <= 49) {
            +     // L3: priority 0-49
            +     pushbackproclist(&l3_queue, pn);
            + }
            + else {
            +     // 一個保險機制
            +     panic("pushreadylist: pid = %d's priority = %d \n is out of range", p -> pid, p -> priority);
            + }
            ```
    - 同理，目前 `popreadylist` 也是從 readylist 取出最前面的 node，但要改為依照 l1 > l2 > l3 的順序取出
        ```c
        // scheduler managed, pop from ready list
        // implementation step 2
        struct proc*
        popreadylist()
        {
        struct proc *p;
        struct proclistnode *pn;
        /*
        if((pn = popfrontproclist(&readylist)) == 0) {
            return 0; // no runnable processes
        }
        p = pn->p;
        freeproclistnode(pn);
        return p;
        */

        // 優先從 l1 取
        + if((pn = popsortedproclist(&l1_queue)) != 0) {
        +     p = pn ->p ;
        +     freeproclistnode(pn);
        +     return p;
        + }

        + // l1 沒了就從 l2 取
        + if((pn = popsortedproclist(&l2_queue)) != 0) {
        +     p = pn ->p ;
        +     freeproclistnode(pn);
        +     return p;
        + }

        + // l2 沒了就從 l3 取
        + if((pn = popfrontproclist(&l3_queue)) != 0) {
        +     p = pn ->p ;
        +     freeproclistnode(pn);
        +     return p;
        + }

        + // 三個 queue 都沒有
        + return 0;
        }
        ```
        雖然 pop 邏輯都一樣，但 xv6 有分為 `popsortedproclist` 及 `popfrontproclist`，整體來說就是取出 node 並修改結構
    - 在前面 trace code 時就發現，`scheduler` 中目前缺少 `swtch` 的過程，這會導致被選中的 process 無法執行，系統跳不出 scheduler，所以現在要先解決這個問題
        ```c
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
            
            + //implementation step 2
            + swtch(&c -> context, &p -> context);

            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;

            release(&p->lock);
        }
        }           
        ```
    - 修改 `implicityield`
        - `implicityield`透過 `devintr` 如果發現是 timer interrupt 就會被呼叫，接著會去檢查現在擁有 CPU 的 process 是不是已經執行超過一定的時間，如果是的話就會呼叫 `yield`進而放棄 CPU。原本的系統是預設所有的 process 都會走這個流程，但依據 SPEC，只有 l3 的 process 會使用 RR 演算法。因此需要先確定 priority 再依據 ticks 決定是否放棄 CPU
        ```c
        void
        implicityield(void)
        {
        struct proc *p = myproc();
        // if(ticks - p->startrunningticks >= 1) {
            // yield round robin scheduling
            // actually ticks - p->startrunningticks should be 1
            // yield();
        // }
        // implementation step 2
        // 先確認 process 是不是在 l3
        + if(p -> priority >= 0 && p -> priority <= 49) {
        +     // l3 是要求 10 個 ticks
        +     if(ticks - p->startrunningticks >= 10) {
        +     yield();
        +     }
        + }
        // 先不管 l1, l2
        }
        ```

3. 實作 L2
    - 這部分要做的事情是把前面 `initsortedproclist` 的比較函數實作（實作在 kernel/proc.c 中）
        ```c
        // implementation step3
        int 
        l2_cmp(struct proc *p1, struct proc *p2)
        {
        if(p1 -> priority > p2 -> priority) {
            return 1;
        }
        if(p1 -> priority < p2 -> priority) {
            return -1;
        }

        // p1 -> priority == p2 -> priority
        if(p1 -> pid < p2 -> pid) {
            return 1; // p1 id 小 優先
        }
        if(p1 -> pid > p2 -> pid) {
            return -1; // p1 id 小 優先
        }

        return 0;
        }
        ```
    - 修改 `proclistinit()` 中的 `initsortedproclist(&l2_queue, 0)`
        ```c
        // initialize process list related data structures.
        void
        proclistinit(void)
        {
        int i;
        // initialize proclistnodes.
        for(i = 0; i < NPROCLISTNODE; i++){
            // implementation step 1
            // proclistnodes 是一個存有 256 個 proclistnode 的陣列
            // 先把它們都變 unused
            proclistnodes[i].used = 0;
            // implementation step 1
            // initlock 實作在 kernel/spinlock.c 中
            // 初始化每個 proclistnode 的鎖，初始狀態是 未上鎖，並且沒有 CPU 持有這個鎖，然後給一個名字
            // 每個鎖給同樣的名字沒問題，因為這只是用來 debug 的，實際上還是看鎖的 記憶體位址
            initlock(&proclistnodes[i].lock, "proclistnode");
        }
        // initialize readylist.
        // initproclist(&readylist);
        // initialize channels.
        // implementation step 1
        // 原本只初始化 readylist
        // 但現在要初始化 l1, l2, l3 queue
        initproclist(&l3_queue);

        // cmp 先傳入 NULL 之後再實作
        // implementation step 3
        // 補上 l2 比較函數
        - initsortedproclist(&l2_queue, 0);
        + initsortedproclist(&l2_queue, l2_cmp);
        initsortedproclist(&l1_queue, 0);

        for(i = 0; i < NCHANNEL; i++){
            channels[i].used = 0;
            initproclist(&channels[i].pl);
            initlock(&channels[i].lock, "channel");
        }
        }
        ```
    - 這個 cmp function 會在 kernel/proc.c 的 `pushsortedproclist` 中產生作用
        ```c
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
        ```
        當要把一個 process 放入 ready queue 中，就會從 ready queue 的頭（哨兵節點）一路檢查到尾端（也是哨兵節點）<br>對每一個節點會使用 `sortedproclist` 的 cmp function 進行比較，如果確認回傳值大於 0（代表要插入的 proclistnode 優先級高於目前比較的 proclistnode） break 出迴圈並開始插入在目前比較的 proclistnode 之前。

4. 實作 L1
    - 這部分要做的事情是把前面 `initsortedproclist` 的比較函數實作（實作在 kernel/proc.c 中）
        ```c
        int
        l1_cmp(struct proc *p1, struct proc *p2)
        {
        int p1_remaining_t = p1 -> t_i - p1 -> T;
        int p2_remaining_t = p2 -> t_i - p2 -> T;

        // Rule 1: Shorter remaining time first
        if(p1_remaining_t < p2_remaining_t) {
            return 1;  // p1 剩餘時間短 優先
        }
        if(p1_remaining_t > p2_remaining_t) {
            return -1;  // p2 剩餘時間短 優先
        }
        
        // Rule 2: Same remaining time, smaller pid first
        if(p1->pid < p2->pid) {
            return 1;  // p1 id 小 優先
        }
        if(p1->pid > p2->pid) {
            return -1;  // p2 id 小 優先
        }
        
        return 0;
        }
        ```
    - 這個 cmp function 會在 kernel/proc.c 的 `pushsortedproclist` 中產生作用
    - 實作更新 `T`，只有在 process 屬於 l1 且狀態是 `RUNNING` 時需要更新，可以隨著 `clockintr()` 每次更新 ticks 時，同步更新 `T`
        ```c
        void
        clockintr()
        {
        acquire(&tickslock);
        ticks++;
        // implementation step4
        struct proc *p = myproc();
        // 檢查 myproc() != 0 是要防止:
        // 1. Scheduler 正在等待 process (c->proc = 0)
        // 2. Process 切換的間隙 (scheduler 剛清空 c->proc)
        // 3. 系統初始化或所有 process 結束

        // 檢查 state == RUNNING 是要防止:
        // Process 剛進 yield(),state 已改為 RUNNABLE
        // 但還沒完全切換到 scheduler
        // 此時 T 不應該累積
        if(p != 0 && p -> state == RUNNING) {
            if(p -> priority >= 100 && p -> priority <= 149) {
            p -> T++;
            }
        }
        // 實作在 kernel/proc.c
        wakeup(&ticks);
        release(&tickslock);
        }
        ```
        要檢查 `myproc()` 的回傳以免回傳值是 0，這是要防止：
        - Scheduler 正在等待 process (c->proc = 0)
        - Process 切換的間隙 (scheduler 剛清空 c->proc)
        - 系統初始化（kernel/main.c 中 mian 函數進入 scheduler）或所有 process 結束
        還要檢查 p -> state 是不是 `RUNNING`，這是要防止：
        - Process 剛進 yield(),state 已改為 `RUNNABLE` 但還沒完全切換到 scheduler
        - 此時 `T` 不應該累積
    - 實作更新 `t_i` 還有更新 `T` 為 0（依據 SPEC 這是在 RUNNING -> WAITING 時需要更新的），所以在 kernel/proc.c 中的 `sleep` 函數實作
        ```c
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

        + // RUNNING -> WAITING 更新 l1 process 的 t_i
        + if(p -> priority >= 100 && p -> priority <= 149) {
        +     p -> t_i = (p -> T + p -> t_i) / 2;
        +     p -> T = 0; // 重置 T
        + }

        // Go to sleep.
        p->chan = chan;
        p->state = SLEEPING;
        .
        .
        .
        }
        ```
    - 接著實作 l1 ready queue 內部的 preemption，因為這種 preemption 的情況會發生在有 new process 進到 ready queue，而從 SPEC 中可以看到進到 `READY` 的三個路徑：
        1. `New` -> `Ready`
            - userinit -> allocproc -> pushreadylist
            - fork or priorfork -> allocproc -> pushreadylist
        2. `Running` -> `Ready`
            - kerneltrap, usertrap -> yield -> pushreadylist -> sched -> kernel/switch.S:swtch
        3. `Waiting` -> `Ready`
            - clockintr -> wakeup
        
        共通點都是會透過 `pushreadylist`（`Waiting` -> `Ready` 雖然只有寫 wakeup，但 wakeup 會呼叫 `pushreadylist`）。因此可以從 `pushreadylist` 做修改

        `pushreadylist` 目前做的事情是：
        - 將 process 打包成 node
        - 依據 process 的 priority 決定要加入哪一個 ready queue

        因此，針對 l1 ready queue 內部的 preemption，只需要修改 l1 ready queue 的部分，修改的地方如下：

        1. 當有新的 process 透過 `pushreadylist` 要加入 ready queue 時，如果新 process 是 l1 level，那就要考慮他是否需要進行 l1 level preemption
        2. 透過 remaining time 的比較，確定能否取代現在正在執行的 process
        3. 如果不行，那就繼續執行舊 process
        4. 如果可以，那就要執行 `yield()`，在 `yield()` 中會：
            - 舊 process 會改變狀態為 `RUNNABLE`
            - 舊 process 被重新 push 到 readylist
            - 執行 `sched` 做 `scheduler` 選擇新的 process，這樣會 work 是因為如果新 process 剩餘執行時間比舊 process 少，那它一定會被放在 l1 ready queue 的 head，所以 `scheduler` 肯定會挑中新 process，以此達到 preemptive 的效果
        ```c
        void
        pushreadylist(struct proc *p)
        {
        struct proclistnode *pn;
        // 創建一個節點來包裝 process
        if((pn = allocproclistnode(p)) == 0) {
            panic("pushreadylist: allocproclistnode");
        }
        // 把節點加到 ready list 的尾端
        // pushbackproclist(&readylist, pn);

        // 要依據 priority 決定要放在哪一個 ready queue
        if(p->priority >= 100 && p->priority <= 149) {
            // L1: priority 100-149
            pushsortedproclist(&l1_queue, pn);

        +     // implementation step 4
        +     // 關於 l1 內部的 preemptive
        +     struct proc *cur = myproc();
        +     if(cur != 0 && cur -> state == RUNNING && cur -> priority >= 100 && cur -> priority <= 149) {
        +     int cur_remaining = cur -> t_i - cur -> T;
        +     int new_remaining = p -> t_i - p -> T;

        +     if(new_remaining < cur_remaining) {
        +         yield(); // 現在的 process 就 yield
        +     }
        +     }
        }
        else if(p->priority >= 50 && p->priority <= 99) {
            // L2: priority 50-99
            pushsortedproclist(&l2_queue, pn);
        }
        else if(p->priority >= 0 && p->priority <= 49) {
            // L3: priority 0-49
            pushbackproclist(&l3_queue, pn);
        }
        else {
            // 一個保險機制
            panic("pushreadylist: pid = %d's priority = %d \n is out of range", p -> pid, p -> priority);
        }
        }
        ```

    - 接著實作跨 queue 的 preemption，由於跨 queue 的 preemption 如果會執行，順序應該是
        - 先加入 readylist
        - 比對目前正在執行的 process 和新加入的 process 之優先級
        - 如果新加入 process 優先級較高，則目前正在執行的 process 要呼叫 `yield()` 來放棄 CPU
        
        因此還是把它實作在 kernel/proc.c 的 `pushreadylist` 函數中

        修改的地方包括
        
        1. 新增一個 flag（`should_yield`） 作為統一的判斷。前面先做邏輯判斷決定 flag 的值，後面再一起做執行 
            ```c
            void
            pushreadylist(struct proc *p)
            {
            struct proclistnode *pn;
            // 創建一個節點來包裝 process
            if((pn = allocproclistnode(p)) == 0) {
                panic("pushreadylist: allocproclistnode");
            }

            // implementation step 5
            + struct proc *cur = myproc();
            + int should_yield = 0;
            ```
            ```c
            // implementation step 5
            + if(should_yield) {
            +     yield();
            + }
            ```
        2. 如果新加入的是 l1 level 的 process，那有兩個情況他可以搶佔
            - 目前執行的 process 是 l2 level or l3 level
            - 目前執行的 process 是 l1 level 但 remaining time 比新加入的 process 長
            ```c
            if(p->priority >= 100 && p->priority <= 149) {
                // L1: priority 100-149
                pushsortedproclist(&l1_queue, pn);

                // implementation step 4
                // 關於 l1 內部的 preemptive
                if(cur != 0 && cur -> state == RUNNING) {
                    
                +     // 條件 2
                    if(cur -> priority >= 100 && cur -> priority <= 149) {
                        int cur_remaining = cur -> t_i - cur -> T;
                        int new_remaining = p -> t_i - p -> T;
                        
                        // implementation step 5
                        if(new_remaining < cur_remaining) {
                        should_yield = 1; // 現在的 process 就 yield
                        }
                    }
                +     // 條件 1
                    else {
                        // cur 不屬於 l1 queue，可以直接搶佔
                        should_yield = 1;
                    }
                }
            }
            ```
        3. 如果新加入的是 l2 level 的 process，那只有一個情況他可以搶佔
            - 目前執行的 process 是 l2 level
            ```c
            else if(p->priority >= 50 && p->priority <= 99) {
                // L2: priority 50-99
                pushsortedproclist(&l2_queue, pn);

                // implementation step 5
                + // 條件 1
                if(cur != 0 && cur -> state == RUNNING) {
                    if(cur -> priority >= 0 && cur -> priority <= 49) {
                        should_yield = 1;
                    }
                }
            }
            ```
