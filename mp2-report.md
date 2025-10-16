# MP2_report_38

## Team Member & Contributions

 * ## **資應碩二 113065530 許恩嘉**
 
 * ## **資工碩二 113062636 吳征彥**



## Trace Code

### 1. Setup timer interrupt
這可以從系統啟動會執行的 kernel/start.c 開始說明。
在系統一開始啟動時，透過 kernel/kernel.ld 告訴 CPU 要從 _entry 符號開始執行

```c
OUTPUT_ARCH( "riscv" )
// 從 _entry 符號開始執行
ENTRY( _entry )

SECTIONS
{
  ...
}
```

而 _entry 定義在 kernel/entry.S 中，它會做一系列動作然後 call start() 函數
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
    e. 透過 w_mtvec 設定中斷處理函數 timervec，中斷發生時會跳到這裡執行。因為 xv6 中，M-mode 只處理時鐘中斷，所以可以這樣設定  
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
    - kernel/kernel.S 的 timervec 在做什麼

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

- r_mhartid() 只能在 M-mode 使用
- 在多核心系統中，所有 CPU 都會執行 start() 函數
- 這樣每個 CPU 都會把自己的 ID 存到 tp
```c
int id = r_mhartid();
w_tp(id);
```
8. 透過 mret 指令回到 S-mode 並呼叫 main function
```c
asm volatile("mret");
```
    
