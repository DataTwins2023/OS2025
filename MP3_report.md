# MP1_report_38

## Team Member & Contributions

 * ## **資應碩二 113065530 許恩嘉**
 
 * ## **資工碩二 113062636 吳征彥**

| 工作項目 | 分工 |
| -------- | -------- |
| Trace Code     |許恩嘉 吳征彥|
| 文件撰寫 | 許恩嘉 吳征彥|
| 功能實作(system call: trace) | 許恩嘉 吳征彥|
| Debug | 許恩嘉 吳征彥|

---

## Trace Code
### 1. How does xv6 run a user program?
#### i. kernel/main.c/main()
在系統啟動時，由 `kernel/entry.S` 開始，呼叫 `start()`，接著切換到 supervisor mode 並呼叫 `main()`
在 `main()` 中一開始就會先判斷是否為 CPU0，只有 CPU0 會進行完整的初始化工作，包含：
- consoleinit
    - 實作在 `kernel/console.c`
    - 首先會初始化一個名為 `cons` 的 lock
    - 初始化 uart（實作在 `kernel/uart.c`）
    - 將 console 的 read, write 操作實際對應到 consoleread, consolewrite
        - consolewrite: user -> kernel -> UART -> console
        - consoleread: console -> UART -> kernel -> user
- printfinit
    - 實作在 `kernel/printf.c`
    - 初始化一個 pr struct的 spinlock（名稱是 `pr`），pr struct 定義在 `kernel/printf.c`
    - 透過 pr.locking = 1 紀錄啟用 lock
- kinit
    - 實作在 `kernel/kalloc.c`
    - 初始化一個名為 `kmem` 的 lock，這個 lock 是 kmem struct 的鎖， kmem struct 負責管理 kernel 的 freelist
    - freerange 是要把從 end（定義在 `kernel/kernel.ld`） 到 PHYSTOP（定義在 `kernel/memlayout.h`） 的所有物理頁面（頁面大小定義在 `user/mp3_*.c`）都釋放，之後要給 kernel 及 user 使用，這邊要注意的是因為 `satp` 暫存器還沒被設定，所以會把 VA 直接當作 PA
- kvminit
    - 實作在 `kernel/vm.c`
    - 會做 `kvmmake`（同樣實作在 `kernel/vm.c`）
    - `kvmmake` 會
        - 透過 `kalloc` 跟 `kernel` 要一塊 page（這是一個 PA）
        - 清空剛剛要到的 page
        - 透過 `kvmmap` 做直接映射（VA = PA），直接映射的範圍包含 `UART0`, `VIRTIO0`, `PLIC`, `KERNBASE`, `etext`
            - `kvmmap` 會再透過 `mappages` 把虛擬地址，對應到實體地址
            - `mappages` 會：
                - 找到 va 範圍對應的起始及終點 page 的起始位址（是 virtual address）
                - 透過 `walk` 找到或是建立 pte，會回傳 level 0 的 pte 指標
                - 如果 pte 的 PTE_V 已經被設定，代表已經被佔用就發出 `panic`
                - 設定 pte 的值
        - 透過 `kvmmap` 把 (uint64)trampoline 映射到 TRAMPOLINE（這是每個 process 的 virtual memory 中最高的一頁），之所以要 mapping 到 virtual memory 的高位址是為了「安全隔離」
        - 做 `proc_mapstacks(kpgtbl)`
            - 實作在 `kernel/proc.c`
            - 會先透過 `KSTACK`（定義在 `kernel/memlayout.h`）計算第 p 個 process 的 kernel stack 虛擬地址
            - 把每個 process 的 kernel stack 虛擬地址對應到實體地址
- kvminithart
    - 實作在 `kernel/vm.c`
    - 透過 `sfence_vma` 確保之前對記憶體的寫入操作都已經完成了
    - 設定 CPU 的分頁表暫存器（也就是 kvminit 設定好的），`satp` 已啟用
    - 透過 `sfence_vma` 清空 TLB 確保使用新的 mapping
    
- procinit
    - 實作在 `kernel/proc.c`
    - 會先初始化兩個 lock
    - 遍歷 `proc` 這個 struct，對每個 proc 都
        - 初始化一個 lock
        - 設定 state 為 `UNUSED`
        - 設定 p -> kstack，這邊可以注意的是當 `satp` 啟用後，這個虛擬地址的存取就會透過分頁表（在 `kvminithart` 設定好了）做轉換
- trapinit
    - 實作在 `kernel/trap.c`
    - 初始化名為 `time` 的鎖
- trapinithart
    - 實作在 `kernel/trap.c`
    - 設定在 kernel 發生 trap 時的向量位址
- plicinit
    - 實作在 `kernel/plic.c`
    - PLIC 是 platform-level interrupt controller 的縮寫（專門管理外部裝置的中斷）
    - 設定哪些外部中斷可以送給 CPU，這邊是 `UART0` 以及 `VIRTIO0`
- plicinithart
    - 實作在 `kernel/plic.c`
    - 設定 CPU 可以接收哪些外部中斷，這邊是 `UART0` 以及 `VIRTIO0`
    - 設定 CPU 的中斷優先權閾值
    - 一個中斷要送到 CPU 有兩個條件
        - 1. 中斷的優先權要大於 CPU 的閾值
        - 2. 中斷要被啟用 (enabled)
- binit
    - 實作在 `kernel/bio.c`
    - 初始化名為 `bcache` 的鎖
    - 初始化 bcache（作為 disk 的記憶體快取層，減少 disk I/O）
    - bcache 以 head 當頭，將所有 buffer 插入到 head 後面,形成雙向環形鏈結串列
- iinit
    - 實作在 `kernel/fs.c`
    - 初始化名為 `itable` 的鎖
    - 初始化 itable（inode 的快取表），一個系統只有一個 itable
    - itable 是存 50 個 inode 的快取陣列
    - inode 是存檔案的 metadata（檔案大小、位置、權限等），每個 inode 也有一個名為 `indoe` 的鎖
- fileinit
    - 實作在 `kernel/file.c`
    - 初始化名為 `ftable` 的鎖
    - 比較一下 `inode` v.s. `file`
        - inode 是檔案的 metadata（檔案大小、位置）
        - file 是 process 開啟檔案的紀錄（檔案 offset、讀寫權限等）
        - 所以一個檔案只會有一個 inode，但可以有多個 file（多個 process 可以同時開啟同一個檔案）
- virtio_disk_init
    - 實作在 `kernel/virtio_disk.c`
    - 初始化核心與 VirtIO 設備之間的協議
- userinit
    - 實作在 `kernel/proc.c`
    - 先做 `allocproc` (同樣實作在 `kernel/proc.c`)    
        - 先找到狀態為 `UNUSED` 的 proc，並取得它的鎖
        - 透過 `allocpid` 拿到 pid 並設定狀態為 `USED`
        - 透過 `kalloc` 分配 trapframe，失敗的話釋放 proc 結構並回傳 0，這個 `kalloc` 雖然回傳 PA，但如果在 kernel 中，因為直接映射，所以可以把 PA 直接轉為 VA
        - 透過 `proc_pagetable`（實作在 `kernel/proc.c`），目的是創建一個 user pagetable。會做以下動作
            - 先透過 `uvmcreate` 申請一個 page，如果失敗就回傳 0
            - 透過 `mappages` 將 VA TRAMPOLINE 映射到 PA trampoline
                - user mode 不能存取 trampoline 頁面 所以沒有 PTE_U 權限
                - 失敗的話釋放 pagetable 結構並回傳 0
                - 對每個 process 而言，他們的 TRAMPOLINE 都是相同的，TRAMPOLINE 對應到的 PA 也是相同的
            - 再透過 `mappages` 做 trapframe 的映射，要把 TRAPFRAME（定義在 `kernel/memlayout.h`） 這個虛擬地址對應到剛剛 `allocproc` 分配到的實體地址
                - 失敗的話要清除 TRAMPOLINE 虛擬地址對應的映射，但保留實際的 physical page
                - 釋放 pagetable 結構並回傳 0
            - 回傳 pagetable，如果失敗的話要釋放 proc

            **kernel page table 跟 user page table 都會有 trapframe 相同的 PA 會映射到不同的 VA**

            **kernel 中是做直接映射**

            **user 中則是映射到一個也是高位址的地方但非直接映射**
        - 清除 p 的內容並設定
            - p->context.ra = (uint64)forkret
            - p->context.sp = p->kstack + PGSIZE;
        - 回傳 p
    - 接著做 `uvmfirst`（實作在 `kernel/vm.c`）
        - 先透過 `kalloc` 跟 kernel 申請一個 page，並清空記憶體
        - 把 VA 0 的位址映射到申請的 page 起點
        - 把 `initcode` 的內容複製到剛分配的 page
    - 設定 p->sz 和 p->trapframe->epc 以及 p->trapframe->sp
        - p->trapframe->epc = 0; 這樣才能在返回 user space 時從 VA 0 開始執行，也就是 `initcode` 的起點
        - p->trapframe->sp = PGSIZE; 代表 stack 從 page 的頂點開始，因為剛剛申請給 `initcode` 的大小就是一個 page
    - 透過 `safestrcpy` 指定 p 的名稱為 `initcode`
    - 設定 cwd 為檔案系統的 root
    - 把 p 狀態改為 `RUNNABLE`
- __sync_synchronize
    - 目的是要確保所有記憶體寫入操作完成，防止 CPU 重新排序指令
- 設定 started = 1

其他 CPU 則等待 CPU0 完成初始化後，只做跟自身相關的硬體初始化，包含：
- __sync_synchronize
    - 確保其他 CPU 能看到 CPU 0 的初始化結果
- kvminithart
    - `kvminit` 建立 kernel page table，這是一個 global 的變數
    - 其他 CPU 只要透過 `kvminithart` 啟用分頁即可
    - 這代表 kernel_pagetable 是每個 CPU 共用的
- trapinithart
    - `trapinit` 設定的 `tickslock` 也是 global 共享
    - 其他 CPU 只需要透過 `trapinithart` 設定 trap vector
    - 因為每個 CPU 都有自己的 `stvec` register
- plicinithart
    - `plicinit` 設定哪些外部中斷可以送給 CPU，這是一個 global 的設定
    - `plicinithart` 是關於每個 CPU 的中斷配置，所以每個 CPU 都要做一次
#### ii. kernel/proc.c/scheduler()
所有 CPU 都初始化完成後，就跳到 `scheduler()`（實作在 `kernel/proc.c` 中），目前只有一個 `RUNNABLE` 的程序，看是由哪個 CPU 得到

得到 process 之後，會做 `swtch`（實作在 `kernel/swtch.S`），沒得到的 CPU 會繼續在 `scheduler` 中執行迴圈
#### iii. kernel/switch.S
`swtch.S` 中的 `swtch` 片段會儲存舊的狀態，並且載入新的狀態

而在 `scheduler` 中的 `swtch` 舊的狀態就是 cpu -> context，新的狀態則是被選中的 p (initcode) -> context

接著做 `ret` ，目的是跳到 p (initcode) 的 ra 存的地址，也就是 `userinit` 中 `allocproc` 設定的 `forkret` 函式地址
#### iv. kernel/proc.c/forkret()
在 `scheduler` 中執行完 `swtch` 後會跳到 `forkret` 中
`forkret` 會做以下事情
- 先把 first variable 設為 1
- 釋放鎖，因為在 `scheduler` 中會持有 process 的鎖
- 判斷是否為 first，如果是的話要 `fsinit`（實作在 kernel/fs.c 中）並且把 first 設為 0。
    - 因為 fsinit(ROOTDEV) 只需要做一次
- 呼叫 `usertrapret`
#### v. kernel/trap.c/usertrapret()
`forkret` 中呼叫 `usertrapret` 後跳到 `kernel/trap.c/usertrapret`。
在這之中，會做：
- 取得目前的 process
- 關閉中斷
- 設定 stvec 為 `kernel/trampoline.S` 中 uservec 的地址，因為接下來要回到 user space
- 設定 trapframe 中儲存的 kernel state，讓下次 process 從 user mode 切到 kernel mode 時可以知道要用哪個 page table、哪個 stack 還有處理函式
- 讀取當前的 sstatus register，清除 SPP （這樣執行 `sret` 時才會回到 user mode），並設置 SPIE （返回 user mode 時啟用中斷），然後寫回 sstatus register
- 設定 sepc register 為 p->trapframe->epc（前面在 `userinit` 時有設為 0）
- 把 p->pagetable 轉為 satp register 格式
- 跳到 `userret`（參數是 satp）
#### vi. kernel/trampoline.S/userret: (Note: In userinit function, we put initcode into the user page table)
`userret` 是要透過 `trapframe` 恢復 user mode 的暫存器

接著 `sret` 會做三件事
- 模式切換
- sstatus.SIE = sstatus.SPIE;
- 程式跳轉（跳到 `sepc` 的位址，也就是 `usertrapret` 中設定的 0）

在 `userinit` 的 `uvmfirst` 有做兩件事情
- 把 VA 0 的位址映射到申請的 page 起點
- 把 `initcode` 的內容複製到剛分配的 page

也就是說接下來要去執行 `initcode`（在 `kernel/proc.c` 中）

`initcode` 是一個呼叫 exec("/init") 的 user program
#### vii. kernel/exec.c/exec()
`initcode` 的目的就是執行 exec("/init")
當 `initcode` 執行 exec("/init") 時，會呼叫到 `kernel/exe.c` 中的 `exec`，接著會：
- 根據 "/init" 路徑找到檔案
- 創建一個新的 pagetable
- 解析 "/init" 的 ELF 檔案格式，把程式碼跟其他結構載入並且分配物理記憶體和建立頁表映射
    - 載入程式：
        - 遍歷 /init 中的每個 ph
        - 如果 ph.type 是可載入
            - 呼叫 uvmalloc 分配虛擬記憶體空間以及 physical page
            - 呼叫 loadseg 把 segment 的內容載入到記憶體
    - 分配 stack
        - kernel 分配兩個頁面，第一個做 stack guard，不可存取。另一個則是 user stack
    - 設置參數：
        - 這邊會做兩次的 copyout:
            - 第一次是把字串本身從 kernel space 複製到 sp，並且把 ustack 中記錄的 `argc` 位址改為 sp，但需要注意的是，ustack 本身仍在 kernel space
            - 第二次是把 ustack 中的陣列指標複製到 sp
    - p -> trapframe -> a1 也就是第二個參數指向 sp
    - 從新程式的 path 提取檔名（不包含目錄）然後存成 p->name
    - 提交新的程序狀態，並且釋放 `initcode` 佔用的舊物理記憶體頁面和 pagetable。
    
    中間有一塊要注意

    **為什麼要先儲存舊分頁表再釋放？**
    
    **因為如果前面沒有 oldpagetable = p->pagetable**
    **proc_freepagetable(oldpagetable, oldsz)就會變成 proc_freepagetable(pagetable, oldsz)，這樣就錯了，因為 pagetable 已經是新的分頁表**
                

### 2. How does xv6 allocate physical memory and map it into the process’s virtual address space?
這個過程包含兩個動作
- 分配 physical memory
    - 從 physical space 找到 empty page
    - mark 這些 page
- map 到 process 的虛擬空間
    - 修改 process 的 page table
    - 建立 physical addr 跟 virt addr 的 mapping 關係
#### i. user/user.h/sbrk()
``` c
char *sbrk(int);
```
這是一個對於 system call 的宣告
傳入的參數是 `int`，返回的是指向新分配空間起始位址的 pointer
#### ii. user/usys.pl
``` pl
entry("sbrk");
```
目的是要生成 `user/usys.S` 中的組合語言片段，生成如下

``` assembly language
.global sbrk
sbrk:
 li a7, SYS_sbrk
 ecall
 ret
```
#### iii. kernel/sysproc.c/sys_sbrk()
經過一系列的 system call 觸發過程（這在 MP1 中有 trace 過，這邊就不細講），處理過程會跳到 `kernel/sysproc.c/sys_sbrk()`

```c
uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if (growproc(n) < 0)
    return -1;
  return addr;
}
```

這部分在做的事情如下：
- argint(0, &n): 
    - 提取第 0 個參數
    - 把值存到 n 所存的位址
    - 以 `sbrk(1024)` 來說就是把 1024 存到 n 這個 variable 中
- addr = myproc() -> sz:
    - `myproc()` 被實作在 `kernel/proc.c` 中，目的是 return the current struct proc *
    - `proc` 結構被定義在 `kernel/proc.h` 之中，而 proc -> size 就是紀錄 Size of process memory (bytes)
    - 這個指令的目的就是回傳目前 process 所使用的 virtual memory 大小，因為 virtual memory 從 0 開始，所以這個大小同時也是目前 process 的結束地址。並且要把這個地址存到 `addr` 變數中

#### iv. kernel/proc.c/growproc()
在 `sys_sbrk()` 中會呼叫 `growproc(n)`（實作在 `kernel/proc.c` 中），這個 `n` 是 user 傳入的參數
```c
int growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
    {
      return -1;
    }
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}
```

做的事情：
- 透過 myproc() return the current struct proc *
- `sz` variable 保存 Size of process memory (bytes)
- 判斷傳入的參數 `n`
    - 大於 0
        - 呼叫 `uvmalloc`（實作在 `kernel/vm.c`）
        - 如果 `uvmalloc` reutrn 0 代表失敗，則 `growproc` return -1
    - 小於 0
        - 呼叫 `uvmdealloc`（實作在 `kernel/vm.c`）
- 更新 Size of process memory
#### v. kernel/vm.c/uvmalloc()
```c
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if (newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz; a += PGSIZE)
  {
    mem = kalloc();
    if (mem == 0)
    {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R | PTE_U | xperm) != 0)
    {
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}
```
做的事情：
- 判斷 `newsz` 跟 `oldsz` 的大小
    - if `newsz` < `oldsz`
        - return `oldsz`
        - **為什麼不需要做 page table 的調整，甚至是回傳 `oldsz` 而非 `newsz`**
        
            這是因為 `uvmalloc` 這個函數本來就是被設計來「增加」process 的 virtual memory（這可以從 `growproc` 會判斷 `n` 是否大於 0 來決定呼叫 `uvmalloc` 還是 `uvmdealloc` 來發現）。因此如果 `newsz` < `oldsz` 就代表新的大小比目前大小還要小，這不屬於 `uvmalloc` 的職責。而是要由 `uvmdealloc` 負責，所以當 `uvmalloc` 知道這不是它負責的任務就只需要直接退出，不用做任何分配或是 page table 的修改。
    - else
        - oldsz = PGROUNDUP(oldsz): 把目前的 process 結束地址向上對齊到 page 的邊界。之所以這樣做的原因可以從後續的 for 迴圈和 mappages() 觀察，每次的分配都是以一個 page 的大小為單位。如果一開始沒有去對齊 page 的邊界，後續的分配迴圈就會嘗試從一個非頁面對齊的虛擬地址開始建立映射，這會讓 page table 無法正確地映射記憶體，因為 page table entry 只能管理從頁面起始地址開始的完整頁面。
        - for 迴圈從 `oldsz` 開始 iterate 到 `newsz` 之間的所有 virtual memory page （以 page 大小為單位）分配 physcial memory 的 page 並建立 mapping
            - 透過 `kalloc`（實作在 `kernel/kalloc.c`） 拿到一個 physical memory page 把回傳的記憶體位址放到 variable `mem`，根據 `mem` 的值
                - 如果回傳 0，代表 memory cannot be allocated，執行 `uvmdealloc`，目的是要 roll back 這次分配已經完成的部分，傳入的參數是 process 的 pagetable（`uvmdealloc` 的形式參數 `pagetable`）、 a （`uvmdealloc` 的形式參數 `oldsz`）以及 oldsz （`uvmdealloc` 的形式參數 `newsz`）
                    - `uvmdealloc` 做：
                        - 首先比較 `a`（`uvmdealloc` 的形式參數 `oldsz`）和 `oldsz`（`uvmdealloc` 的形式參數 `newsz`）的大小，如果 `oldsz`（`uvmdealloc` 的形式參數 `newsz`） >= `a`（`uvmdealloc` 的形式參數 `oldsz`），這代表在分配第一個 page 時就出問題，但因為還沒分配任何的 physical memory 所以不需要做任何事情
                        - 如果不是在分配第一個 page 時就出問題，那 `a`（`uvmdealloc` 的形式參數 `oldsz`） 就會比 `oldsz`（`uvmdealloc` 的形式參數 `newsz`）大
                            
                            **呼叫 `uvmdealloc` 的 `uvmalloc` 不是已經做過 page-align 且分配的 memory 單位都是 page 的大小，為什麼 `uvmdealloc` 中還要再做一次？ 原因是uvmdealloc 不只被 uvmalloc 呼叫，所以還是要加入這層做確保有 page-align**
                            
                            - 計算有多少從`a`（`uvmdealloc` 的形式參數 `oldsz`） 到 `oldsz`（`uvmdealloc` 的形式參數 `newsz`）間有多少 pages
                            - 呼叫 `uvmunmap`（實作在 `kernel/vm.c`），`uvmunmap` 做：
                                - 確認輸入的 virtual address 有 page-align
                                - iterate 每個 virtual memory 的 page，判斷：
                                    - 如果找不到對應的 page table entry 就出現 `panic`
                                    - 如果 `PTE_V` 沒有被設定，代表是無效的 mapping，也會出現 `panic`
                                    - 透過 `PTE_FLAGS` 巨集(實作在 `kernel/riscv.h`)，確認對應的 page table entry 設定的 flag。那為什麼  == PTE_V 要回報 `panic`，是因為 RISC-V 規格規定:
                                        - Non-leaf page table entry（指向下一級頁表的指標）只會設定 `PTE_V`
                                        - leaf page table entry（指向 physical page）需要設定 PTE_V 以及至少一個權限位元
                                    - 判斷 `do_free` 為真，透過 `PTE2PA` 把 pte 中的 PPN 取出再透過 bit operation 轉為 physical address
                                    - 呼叫 `kfree`（實作在 `kernel/kalloc.c`） 把要釋放的 page 內容都填為 1 ，然後將其包裝成 run struct 並加入 kmem.freelist
                        
                        總結來說 `uvmdealloc` 負責清除頁面在 page table 中的 mapping 關係，並 free 實體記憶體
                - 回傳不為 0，代表有分配到 memory，接著執行以下步驟
                    - 透過 `memset` 把分配到的 page 內容設為 0
                    - 做 `mappages`（實作在 `kernel/vm.c`），把 `a` 也就是 `oldsz` 開始 iterate 到 `newsz` 之間的所有 virtual memory page 的起始位址映射到前面透過 `kalloc` 得到的 `mem` 位址
                        - 如果回傳 != 0 ，代表失敗，那就需要透過 `kfree` 釋放 `mem` 並且透過 `uvmdealloc` 把已經分配及 mapping 完成的部分做 rollback 並 return 0
    - 順利結束 for 迴圈，回傳 new size

    總結來說 `uvmalloc` 負責在 process 的 virtual memory 中，從完成 page-align 的舊地址開始到新地址為止，以 page 大小為單位，迭代做 physical memory 的分配和 page table mapping，如果任何步驟失敗，透過 roll back `uvmdealloc` 確保已分配完成的部分被釋放，最後回傳成功擴展後的記憶體大小


## implementation
### 1. Print Page Table
這部分的目的是要印出一個 process 的 page table 中所有使用中的 page table entry 資訊。

我們透過 `kernel/vm.c` 中的 `vmprint` 來實作，但因為此函數的參數只有 `pagetable`，但因為要完成遍歷 page table 的目標，需要更多變數包含 `level` 以及 `va_base`，所以選擇另外實作 `printwalk` 函數，該函數的參數包括 `pagetable`、`level` 以及 `va_base`。並且透過 `vmprint` 來呼叫。

在 `vmprint` 中
```c
void vmprint(pagetable_t pagetable)
{
  /* mp3 TODO */
  printf("page table %p\n", pagetable);
  printwalk(pagetable, 2, 0);
}
```
只需要負責印出 `pagetable` 的位址，並且呼叫 `printwalk`

而在 `printwalk` 中
```c
void printwalk(pagetable_t pagetable, int level, uint64 va_base)
{
  for(int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if (pte & PTE_V) // valid
    {
      uint64 pa = PTE2PA(pte); // 取得目前 pte 中 PPN 的部分，也就是下一層 page table 的位址

      
      uint64 va = va_base | (i << PXSHIFT(level)); // PXSHIFT 定義在 kernel/riscv.h
      int ind_space = 2 * (3 - level);
      for(int j = 0; j < ind_space; j++) {
        printf(" ");
      }

      char flags_str[16] = {0};
      int idx = 0;

      if(pte & PTE_V) flags_str[idx++] = 'V';

      if(pte & PTE_R) {
        if(idx > 0) flags_str[idx++] = ' ';
        flags_str[idx++] = 'R';
      }
      if(pte & PTE_W) {
        if(idx > 0) flags_str[idx++] = ' ';
        flags_str[idx++] = 'W';
      }
      if(pte & PTE_X) {
        if(idx > 0) flags_str[idx++] = ' ';
        flags_str[idx++] = 'X';
      }
      if(pte & PTE_U) {
        if(idx > 0) flags_str[idx++] = ' ';
        flags_str[idx++] = 'U';
      }

      printf("%d: pte=%p va=%p pa=%p %s", 
             i, pte, va, pa, flags_str);
      
      printf("\n");


      if(level > 0 && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
        // 條件成立代表還有下一層級的 page table
        // 遞迴呼叫印出下一層級的 page table
        printwalk((pagetable_t)pa, level - 1, va);
      }
    }
  }
}
```
這是一個遞迴的函數，首先會遍歷 level 2 的 512 個 entries，並且：
- 透過 `(pte & PTE_V)` 檢查是否 valid（有在使用），沒有的話就繼續下一個 entry，有則繼續往下
- 透過 `PTE2PA` 計算 pa（目前 pte 中 PPN 的部分，也就是下一層 page table 的位址）
- 透過 `PXSHIFT`（定義在 `kernel/riscv.h` 中）計算 va
- 檢查每個 PTE flag（`PTE_V`, `PTE_R`, `PTE_W`, `PTE_X`, `PTE_U`）
- 印出結果
- 接著要檢查如果目前 level > 0 且 (`PTE_R`, `PTE_W`, `PTE_X`) 都沒有被設定，那代表目前的 page table 不是 leaf（這是 RISC-V 的規定），還需要往下遞迴去印出更多的 entries，而下一次遞迴 `printwalk(pagetable_t pagetable, int level, uint64 va_base)` 的參數決定方式是：
    - `pagetable`：這是下一個 level 的 pagetable 之 PA，可以透過前面 `uint64 pa = PTE2PA(pte);` 得到
    - `level`：這要填入下一層 level 的值，也就是現在的 level - 1
    - `va_base`：使用前面計算出的 va，作為下一層遞迴的地址基底


### 2. Add a read only share page
這個實作的目的是要讓每個 procss 得到一個 read-only 的 `USYSCALL` page，然後把它 mapping 到固定的 virtual address `USYSCALL`，讓 user 可以「直接讀取」而不需要透過 syscall 做「狀態的轉換」，為了達到這個目的，需要做以下步驟：
- 定義 struct 及變數（實作在 `kernel/proc.h`）
    ```c
    // Per-process state
    struct proc
    {
        struct spinlock lock;

        // p->lock must be held when using these:
        enum procstate state; // Process state
        void *chan;           // If non-zero, sleeping on chan
        int killed;           // If non-zero, have been killed
        int xstate;           // Exit status to be returned to parent's wait
        int pid;              // Process ID

        // wait_lock must be held when using this:
        struct proc *parent; // Parent process

        // these are private to the process, so p->lock need not be held.
        uint64 kstack;               // Virtual address of kernel stack
        uint64 sz;                   // Size of process memory (bytes)
        pagetable_t pagetable;       // User page table
        struct trapframe *trapframe; // data page for trampoline.S
        struct context context;      // swtch() here to run process
        struct file *ofile[NOFILE];  // Open files
        struct inode *cwd;           // Current directory
        char name[16];               // Process name (debugging)
        /* mp3 TODO */
        // implementation 2
        void *usyscallpage;          // a single read-only page mapped at the fixed virtual address USYSCALL
    };
    ```
    `usyscallpage` 是一個指標指向存有 non-sensetive information 的 phsical page，之後會 mapping 到 `USYSCALL` virtual page

- 在初始化 proc 時，`allocproc`（實作在 `kernel/proc.c`）會透過 `kalloc` 分配一個 physical page 給 process 並且設定需要的 usyscall page 內容
    ```c
    static struct proc *
    allocproc(void)
    {
        struct proc *p;

        for (p = proc; p < &proc[NPROC]; p++)
        {
            acquire(&p->lock);
            if (p->state == UNUSED)
            {
            goto found;
            }
            else
            {
            release(&p->lock);
            }
        }
        return 0;

        found:
        p->pid = allocpid();
        p->state = USED;

        // Allocate a trapframe page.
        // return PA addr
        if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
        {
            // 失敗的話釋放 proc 結構並回傳 0
            freeproc(p);
            release(&p->lock);
            return 0;
        }

        // implementation 2
        // 先分配一個 page 作為 usyscall page
        + p -> usyscallpage = kalloc();
        + if (p -> usyscallpage == 0) {
        +     freeproc(p);
        +     release(&p->lock);
        +     return 0;
        + }

        + // 設定 usyscall page 的內容
        + struct usyscall *usp = (struct usyscall *)(p -> usyscallpage);
        + usp -> pid = p -> pid;

        // An empty user page table.
        // proc_pagetable
        p->pagetable = proc_pagetable(p);
        if (p->pagetable == 0)
        {
            // 失敗的話釋放 proc 結構並回傳 0
            freeproc(p);
            release(&p->lock);
            return 0;
        }
        // Set up new context to start executing at forkret,
        // which returns to user space.
        // 清除 context 內容並設定 ra 和 sp
        memset(&p->context, 0, sizeof(p->context));
        // 設定 p->context.ra 為 forkret 函式的位址
        // 定義在 line 558
        p->context.ra = (uint64)forkret;
        p->context.sp = p->kstack + PGSIZE;

        return p;
    }
    ```

- 透過 `proc_pagetable`（實作在 `kernel/proc.c`）完成 user page table 建立時，除了 `trampoline` 及 `trapframe` 之外要再多 mapping `USYSCALL`

    ```c
    pagetable_t
    proc_pagetable(struct proc *p)
    {
    pagetable_t pagetable;

    // An empty page table.
    // 定義在 kernel/vm.c
    // 跟 kernel 要一個空的 page table
    // 透過 kalloc 還是一個 PA
    pagetable = uvmcreate();
    if (pagetable == 0)
        return 0;

    // map the trampoline code (for system call return)
    // at the highest user virtual address.
    // only the supervisor uses it, on the way
    // to/from user space, so not PTE_U.
    // trampoline 用於 在 kernel 和 user space 之間切換
    // 每個 process 共用同一個 trampoline 頁面
    // 所以要映射在每個 process 的 page table 中且位置相同
    // user mode 不能存取 trampoline 頁面 所以沒有 PTE_U 權限
    if (mappages(pagetable, TRAMPOLINE, PGSIZE,
                (uint64)trampoline, PTE_R | PTE_X) < 0)
    {
        uvmfree(pagetable, 0);
        return 0;
    }

    // map the trapframe page just below the trampoline page, for
    // trampoline.S.
    // 映射 trapframe 頁面
    // 每個 process 有自己的 trapframe 頁面 用於 儲存 user space 的暫存器狀態
    // 位置在 trampoline 頁面下方
    if (mappages(pagetable, TRAPFRAME, PGSIZE,
                (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
    {
        // 清除已經映射的 trampoline 頁面
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        // 釋放 page table
        uvmfree(pagetable, 0);
        return 0;
    }

    + // implementation 2
    + if (mappages(pagetable, USYSCALL, PGSIZE,
                (uint64)(p->usyscallpage), PTE_R | PTE_U) < 0)
    + {
    +   // 清除已經映射的 trampoline 頁面
    +   uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    +   // 清除已經映射的 trapframe 頁面
    +   uvmunmap(pagetable, TRAPFRAME, 1, 0);
    +   // 釋放 page table
    +   uvmfree(pagetable, 0);
    +   return 0;
    + }
    return pagetable;
    }
    ```

- 在 process 結束時，透過 `freeproc`（實作在 `kernel/proc.c`），如果有使用 usyscallpage 要透過 `kfree` 釋放 usyscallpage，並將其設為 0
    ```c
    static void
    freeproc(struct proc *p)
    {
        if (p->trapframe)
            kfree((void *)p->trapframe);
        p->trapframe = 0;

        // implementation 2
        + if (p->usyscallpage)
        +     kfree((void *)p->usyscallpage);
        + p->usyscallpage = 0;

        if (p->pagetable)
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
    ```
- 在 `freeproc` 中會呼叫 `proc_freepagetable`（實作在 `kernel/proc.c`），目的是要釋放 process 的 page table 並且釋放 mapping 到的 physical memory（透過 `uvmunmap`），也要記得釋放 `USYSCALL`
    ```c
    void proc_freepagetable(pagetable_t pagetable, uint64 sz)
    {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmunmap(pagetable, TRAPFRAME, 1, 0);
        // implementation 2
        + uvmunmap(pagetable, USYSCALL, 1, 0);
        uvmfree(pagetable, sz);
    }
    ```


### 3. Generate a Page Fault
這部分的目的是要處理 page fault ，為了處理這個問題需要做到幾件事情：
- 在 `kernel/trap.c` 中新增處理 `pagefault` 的過程
    ```c
    void usertrap(void)
    {
        int which_dev = 0;

        if ((r_sstatus() & SSTATUS_SPP) != 0)
            panic("usertrap: not from user mode");

        // send interrupts and exceptions to kerneltrap(),
        // since we're now in the kernel.
        w_stvec((uint64)kernelvec);

        struct proc *p = myproc();

        // save user program counter.
        p->trapframe->epc = r_sepc();
        // mp3 TODO
        if (r_scause() == 8)
        {
            // system call

            if (killed(p))
            exit(-1);

            // sepc points to the ecall instruction,
            // but we want to return to the next instruction.
            p->trapframe->epc += 4;

            // an interrupt will change sepc, scause, and sstatus,
            // so enable only now that we're done with those registers.
            intr_on();

            syscall();
        }
        + // implementation 3
        + else if (r_scause() == 13 || r_scause() == 15)
        + {
        +     if (handle_pagefault() < 0)
        +     {
        +     // 如果 handle_pagefault 失敗，代表無法處理此 page fault，就要 kill process
        +     setkilled(p);
        +     }
        + }
        else if ((which_dev = devintr()) != 0)
        {
            // ok
        }
        else
        {
            printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
            printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
            setkilled(p);
        }

        if (killed(p))
            exit(-1);

        // give up the CPU if this is a timer interrupt.
        if (which_dev == 2)
            yield();

        usertrapret();
    }
    ```
- 在 `kernel/proc.c` 中，`growproc` 的函數本來會同時分配 virtual memory 並透過 `uvmalloc` 來 mapping 到 physical memory，在此要將 mapping 延後到 page fault 過程再處理
    ```c
    int growproc(int n)
    {
        uint64 sz;
        struct proc *p = myproc();

        sz = p->sz;
        if (n > 0)
        {
            // implementation 3
            /* if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
            {
            return -1;
            } */
            // 只先增加虛擬記憶體空間，分配實體記憶體頁面在 page fault 時處理
        +     sz += n;
        }
        else if (n < 0)
        {
            sz = uvmdealloc(p->pagetable, sz, sz + n);
        }
        p->sz = sz;
        return 0;
    }
    ```

- 因為不是所有 virtual memory 的 page 都有 map 到 physical memory，所以也要修改 `uvmunmap`（`kernel/vm.c`） 的邏輯
    - 如果 walk 找不到原本會出現 `panic` ，現在找不到那就繼續移動到下一個 page
    - 如果有找到，那接著要透過 `PTE_V` 去確認是否 `valid`：
        - 如果 `valid` 則透過 `PTE_FLAGS` 來確認 flags 的值：
            - 若只有 `PTE_V` 代表不是 leaf ，出現 `panic`
            - 否則接著確認是否要 `do_free` 要的話就透過 `PTE2PA` 將 pte 轉為 PA 並呼叫 `kfree`
    ```c
    void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
    {
        uint64 a;
        pte_t *pte;

        if ((va % PGSIZE) != 0)
            panic("uvmunmap: not aligned");

        // implementation 3
        for (a = va; a < va + npages * PGSIZE; a += PGSIZE)
        {
            if ((pte = walk(pagetable, a, 0)) == 0)
                // panic("uvmunmap: walk");
                continue;
            if (*pte & PTE_V) // 如果有映射到實體記憶體
            {
                if (PTE_FLAGS(*pte) == PTE_V)
                    panic("uvmunmap: not a leaf");

                if (do_free)
                {
                    uint64 pa = PTE2PA(*pte);
                    kfree((void *)pa);
                }
                *pte = 0;
            }
            /*
            if ((*pte & PTE_V) == 0)
            panic("uvmunmap: not mapped");
            if (PTE_FLAGS(*pte) == PTE_V)
            panic("uvmunmap: not a leaf");
            if (do_free)
            {
            uint64 pa = PTE2PA(*pte);
            kfree((void *)pa);
            }
            */
        }
    }
    ```
- 實作 `handle_pgfault` ，主要目的就是發生 page fault 時分配 physical memory
    ```c
    int handle_pgfault()
    {
        // implementation 3
        /* mp3 TODO */
        // 這是從 kernel/trap.c 如果發現是 page fault 就會呼叫這個函式
        struct proc *p = myproc();
        // 獲取觸發 page fault 的虛擬位址
        /* Find the address that caused the fault */
        uint64 va = r_stval();
        
        // 檢查 VA 是否在合法範圍內（必須在 0 - p->sz 內）
        if (va >= p->sz) {
        return -1;
        }

        // 將 VA 對齊到頁面邊界
        uint64 aligned_va = PGROUNDDOWN(va);

        // 檢查 PTE 是否已經存在
        pte_t *pte = walk(p->pagetable, aligned_va, 0);
        if (pte && (*pte & PTE_V)) {
            // PTE 已經存在且有效，不應該發生 page fault
            return -1;
        }

        // 分配一個新的物理頁面
        char *pa = kalloc();
        if (pa == 0) {
            // 分配失敗
            return -1;
        }

        // 初始化新頁面為 0
        memset(pa, 0, PGSIZE);

        // 建立虛擬地址到物理地址的映射，設置適當的權限
        int perm = PTE_R | PTE_W | PTE_U | PTE_X;

        if (mappages(p->pagetable, aligned_va, PGSIZE, (uint64)pa, perm) < 0) {
            // 映射失敗，釋放分配的頁面
            kfree(pa);
            return -1;
        }

        // 成功處理 page fault
        return 0;
    }
    ```
    具體做法如下：
    - 首先要透過 `r_stval` 取得發生錯誤的 VA （也就是沒辦法順利找到 PA 的虛擬地址）
    - 接著檢查 VA 的合法性，如果超出了 process 的 size 就是非法的
    - 透過 `PGROUNDDOWN` 把 VA 做向下的 page align（雖然 `mappages` 會做，但這邊可以先做）
    - 透過 `walk` 檢查 PTE 是否存在，如果存在且 `PTE_V` 有被設置，代表存在且有效，那不應該發生 page fault，回傳 -1
    - 分配一個新的 physical page，如果有問題也回傳 -1
    - 初始化新的 physical page 內容為 0
    - 設定權限為 `PTE_R | PTE_W | PTE_U | PTE_X`
    - 建立 virtual address 到 physical address 的 mapping 並且設定權限，如果失敗的話釋放實體記憶體並回傳 -1
### 4. Demand Paging and Swapping

為了完成第四部份的需求，我們改動了以下部分的程式碼
- kernel/vm.c: `vmprint()`: 擴充功能讓它可以支援印出被 swap out 的 page 的 Block number
- kernel/vm.c: `madvise()`中的 `MADV_NORMAL`實作: 不做任何事情
- kernel/vm.c: `madvise()`中的 `MADV_DONTNEED`實作: swap out 操作
- kernel/vm.c: `madvise()`中的 `MADV_WILLNEED`實作: swap in 操作

以下講解這四個部分的實作細節。  
#### 修改 vmprint_rec() for 迴圈內處理 pte 的程式碼
##### 說明:  
這邊的 `vmprint()` 函式的程式碼架構與前面報告的 **實作1** 內容相似，但因為我們這組這次的實作方式採各自獨立進行的方式，所以其中遞迴函式的實作有所差異。前面 `printwalk()` 的功能與這邊 `vmprint_rec()` 的功能一模一樣。
```c
void
vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);
  // 這邊遞迴印出不同層的 ptes 的資訊
  vmprint_rec(pagetable, 2, 2, 0); // level=2, indent=2 spaces, base_va=0
}  
```
與前面的實作相同 vmprint_rec() 會遍歷 512 個 PTEs  
```c
for(int i = 0; i < 512; i++){ 
    pte_t pte = pagetable[i];
    /* 處理 pte 的程式碼*/
    
}
```

```c
// 不只 valid 的 page 資訊要印出，同時支援印出被 swap out 的 page 資訊
if(!(pte & PTE_V) && !(pte & PTE_S))
      continue;
```

```c
// 如果是被 swap 過的 page 
if (pte & PTE_S) {
    // 從 PTE 取出其 block number
    uint64 blockno = PTE2BLOCKNO(pte);
    // 印出其 後半部 pte 資訊
    printf("pa=%p blockno=%p", (uint64)blockno << 12, blockno); 
} else { // 如果是一般的 page 則直接印出其 pa
    printf("pa=%p", pa);
}
```

```c
// 印出 flags
if(pte & PTE_V) printf(" V");
if(pte & PTE_R) printf(" R");
if(pte & PTE_W) printf(" W");
if(pte & PTE_X) printf(" X");
if(pte & PTE_U) printf(" U");
if(pte & PTE_S) printf(" S");

printf("\n");
```
```c
// 若是 valid page 且不是最底層(level > 0)，則繼續遞迴 L1/L0
// 避免繼續遞迴被 swap out 的 pte
if((pte & PTE_V) && level > 0) {
    vmprint_rec((pagetable_t)pa, level - 1, indent + 2, va);
}
```

#### 實作 madvise() 之 MADV_NORMAL
在 `kernel/vm.c` 中實作 `madvise()` 當 `advice` == `MADV_NORMAL` 情況  

```c
int madvise(uint64 base, uint64 len, int advice)
{
  struct proc *p = myproc();
  
  // 0. 檢查範圍是否合法
  // base + len 範圍不能超過 process 大小
  if (base > p->sz || (base + len) > p->sz) {
    return -1;
  }
  // 不占空間的 process 不處理
  if (len == 0) return 0;

  // 1. 處理 normal advice: 不做任何事
  if (advice == MADV_NORMAL) {
    return 0;
  }
```
#### 實作 madvise() 之 MADV_DONTNEED
在 `kernel/vm.c` 中實作 `madvise()` 當 `advice` == `MADV_DONTNEED` 情況  
```c
  // 2. 處理 don't need advice: Swap Out 
  if (advice == MADV_DONTNEED) {
    pte_t *pte;
    uint64 start = PGROUNDDOWN(base);
    uint64 end = PGROUNDUP(base + len);


    // 遍歷 base 到 base + len 的所有 pages 
    for (uint64 va = start; va < end; va += PGSIZE) {
      // 透過 walk 取得 pte，不需要分配新的 Page Table
      pte = walk(p->pagetable, va, 0);
      
      // 如果 pte 不存在 or invalid (已經 swapped, 或根本沒分配)，則跳過
      if (pte == 0 || !(*pte & PTE_V))
        continue;

      //Transactional 的磁碟操縱開始，做 swap out 
      begin_op();
      
      // 1. 在 disk 上 allocate 一個 block 並取得其 blockno
      uint blockno = balloc_page(ROOTDEV);
      
      // 2. 透過 pa 與 blockno，把 pa 這個 page 的 mem 內容寫進 disk 中 
      uint64 pa = PTE2PA(*pte);
      write_page_to_disk(ROOTDEV, (char*)pa, blockno);
      
      // 3. 修改 pte
      // Clear Valid bit 和 Set Swapped bit
      // 將 Block Number 存入 PTE (原本放 PA 的位置)
      // 保留原本的權限 Flags (R/W/X/U)
      *pte = BLOCKNO2PTE(blockno) | PTE_FLAGS(*pte) | PTE_S;
      *pte &= ~PTE_V;
      
      // Transactional 的磁碟操縱結束
      end_op();
      
      // 4. free the physical memory
      kfree((void*)pa);
    }
    // 在一連串的 swap out 操作後 pages 處於記憶體的狀態可能大幅改變
    // 在這邊 Flush the TLB，把舊的 TLB 資訊清掉
    sfence_vma();
    return 0;
  }
```
#### 實作 madvise() 之 MADV_WILLNEED
在 `kernel/vm.c` 中實作 `madvise()` 當 `advice` == `MADV_WILLNEED` 情況
這一項目需要做 page 的 swap in 需要考量到三種情況 (以下以 A, B, C 代稱)

- A: page 只在 disk 中 (被 swap out) -> 需要從 disk 中把資料讀回 RAM
- B: page 本來就還沒被 alloc (不在 swap space 中) -> kalloc 一個 page 給它，並載入 mem 中 
- C: page 已經在 RAM -> 不做任何事情(不用 kalloc, 不用 swap in)
```c
  // 3. 處理 will need advice: Swap In
  if (advice == MADV_WILLNEED) {
    pte_t *pte;
    uint64 start = PGROUNDDOWN(base);
    uint64 end = PGROUNDUP(base + len);

    // 遍歷 base 到 base + len 的所有 pages 
    for (uint64 va = start; va < end; va += PGSIZE) {

      // 取得 pte，如果 Page Table 頁面不存在則建立 (alloc=1)
      // 因為我們"Will Need"這塊記憶體，所以必須確保路徑存在
      pte = walk(p->pagetable, va, 1);
      if (pte == 0) return -1; // 系統記憶體不足

      // 情況 A: 頁面在磁碟中 (Swapped)
      if (*pte & PTE_S) {
        uint blockno = PTE2BLOCKNO(*pte);
        
        // 分配新的物理頁面
        char *pa = kalloc();
        if (pa == 0) return -1; // 如果 kalloc 回傳0，表示 run out of mem
        

        begin_op();
        // 從磁碟讀回這個 page 的資料
        read_page_from_disk(ROOTDEV, pa, blockno);
        // release this disk block (因為已經讀回 RAM 了)
        bfree_page(ROOTDEV, blockno);
        end_op();
        
        // 更新 pte: 指向新的 pa，設定 Valid bit，清除 PTE_S
        *pte = PA2PTE((uint64)pa) | PTE_FLAGS(*pte) | PTE_V;
        *pte &= ~PTE_S;
      } 
      // 情況 B: 頁面根本還沒分配 (Lazy Allocation)
      else if (!(*pte & PTE_V)) {
        // 分配一個頁面並寫入 mem 中
        char *pa = kalloc();
        if (pa == 0) return -1;
        memset(pa, 0, PGSIZE);
        
        // 設定新頁面的mapping，並設定完整權限給它
        *pte = PA2PTE((uint64)pa) | PTE_R | PTE_W | PTE_X | PTE_U | PTE_V;
      }
      // 情況 C: 頁面已經在記憶體中 (什麼都不做)
    }
    return 0;
  }
  return 0;
  panic("not implemented yet\n");
}
```