# MP1_report_28

## Team Member & Contributions

 * ## **資應碩二 113065530 許恩嘉**
 
 * ## **資工碩二 113062636 吳征彥**

| 工作項目 | 分工 |
| -------- | -------- |
| Trace Code     |  許恩嘉    |
| 文件撰寫 | 許恩嘉 & 吳征彥 |
| 功能實作(system call: trace) | 許恩嘉 |
| 功能實作(system call: sysinfo) | 吳征彥 |
| Debug | 許恩嘉 & 吳征彥 |

---

## Trace Code 以 read 為例

### 用戶態準備階段
首先從 user/grep.c 出發，在其中會調用到 read 這個 system call。在 user/user.h 中聲明了 read，user/grep.c 調用 read，編譯器生成函數調用指令。user/usys.S（透過 user/usys.pl 生成的）提供了 read 的實際實現，鏈接器將調用和實現連接起來。

### 觸發系統調用
在 user/usys.S 中將 SYS_read 系統調用號載入到 a7 暫存器，然後執行 ecall 指令觸發陷阱，CPU 硬體自動切換到 supervisor 模式並跳轉到內核的陷阱處理程序（進入 kernel）。

### 進入內核 - trampoline.S/uservec
進入內核後透過 kernel/trap.c 的設定自動跳轉到 stvec 指向的地址（理論上每個 process 會相同）也就是 trampoline.S 中的 uservec。

在 uservec 中會：
1. 先把 user a0 register 的值存到 sscratch 中，因為 a0 要拿來存 trapframe 的地址（每個 process 的 trapframe 就會不同）
2. 然後開始一系列將 user register 存到 trapframe 的過程
3. 結束後再將原先 a0 暫存器的值（現在已經在 sscratch）也存到 trapframe 中
4. 設置 kernel stack pointer
5. 將 CPU 核心的編號載入到 tp register 中
6. 將 kernel/trap.c/usertrap 函數地址載入到 t0
7. 將 kernel page table addr 寫到 satp 暫存器（存放使用哪個頁表進行地址轉換）（基本上就是要從 user page table 切換到 kernel page table 但中間要經過一些機制確保有順利切換）
8. 跳到 kernel/trap.c/usertrap 函數執行，注意因為 usertrap 會使用 usertrapret 這會直接回到用戶程式，所以下面的代碼永遠不會執行

### usertrap 函數處理
usertrap 函數會：
1. 先檢查系統調用是否真的來自 user mode
2. 然後把 stvec 設置為 kenrelvec 函數的位址（因為已經進入 kernel mode，這時候如果遇到 trap 應該要跳到 kernel/trap.c 中的 kernelvec 而非 trampoline.S 中的uservec）
3. 取得當前執行的進程之 control block 然後把 trapframe -> epc 設為 r_sepc 函數的值r_sepc() 函數讀取 sepc 寄存器的值，sepc 寄存器保存了發生 trap 時的用戶程式計數器 (PC)。這樣可以在處理完 trap 後，將控制權返回到正確的用戶程式位置
   - 為什麼不在 trampoline.S 裡面存呢？因為 sepc 是一個特殊的暫存器（CSR 暫存器）所以需要透過通用暫存器（原本有一個 sscratch 暫存器可以拿來先放通用暫存器的值，讓通用暫存器可以空下來，但被 a0 拿去使用了，a0 被拿去存 trapframe 地址）來進行中間的過渡然而通用暫存器現在都被原先 process 給使用，如果貿然使用可能會蓋掉原本的值
4. 讀取 sauce register 的值scause 的值是硬體自動判斷 trap 原因及類型就寫入的，軟體不參與，scause 寄存器保存了 trap 的原因
   - 如果是 system call，首先檢查該程序是不是已經被標記為需要終止，如果是的話那就調用 exit(-1) 終止
     - 不用中止的話，要去改動 p -> trapframe -> epc 的值，從 system call 返回後繼續執行下一個指令
     - 重新啟用中斷，因為當 trap 發生時，硬體會自動關閉中斷
     - 做 syscall （kernel/syscall.c）
       - 在 process 的 control block 中透過 trapframe 的 a7 欄位抓到系統調用號碼，經過一些檢查
       - 呼叫 syscalls\[系統調用號碼\]() 函數，回傳值存在 trapframe 的 a0 欄位
   - 另一種情況是設備中斷（這邊包含 timer interrupt）
   - 非預期的中斷
5. 如果程序有被標為需要終止，那就透過 exit(-1) 終止
6. 如果前面的設備中斷是 timer interrupt 那就要做 yield 放棄 CPU
7. 準備返回 user space 透過 usertrapret（kernel/trap.c）

### usertrapret 函數處理
usertrapret（kernel/trap.c）會：
1. 取得現在程序的 PCB
2. 關閉中斷（因為接下來要把 stvec 從 kerneltrap 切換回 usertrap，所以再切換過程中先關閉中斷，避免在中斷期間發生不一致的狀況）
3. 將 stvec 設為 trampoline_uservec，這邊的設定方法有點 tricky 說明如下
   - 因為 trampoline 需要被所有 user process 共享訪問 trampoline.S 在編譯時會有編譯器分配的地址(如 0x80001000) 但在運行時，為了讓所有用戶進程都能訪問（物理記憶體中只有一段 trampoline 代碼，但映射關係會把他們映射到每一個進程的相同位址），會被重新映射到 TRAMPOLINE 高虛擬地址 所以需要計算 uservec 在 trampoline 頁面中的相對偏移量 然後加上運行時的 TRAMPOLINE 基地址 這樣就能得到 uservec 的正確運行時虛擬地址，確保硬體能正確跳轉 uservec 和 trampoline 都定義在 trampoline.S 裡面 TRAMPOLINE 是在 memlayout.h 中定義的高虛擬地址常數
   - uservec 是一個只讀的片段，所以多個 process 共用就沒有一致性的問題
4. 重新設定一些 uservec 會用到的 trapframe 暫存器，讓下次從 user mode 可以順利切換到 kernel mode，包含
   - kernel page table
   - kernel stack pointer
   - usertrap
   - CPU id
5. 把 r_sstatus 的 SPP 位元設為0（為了下次進到 usertrap 的檢查所做的設定）
6. 把 r_sstatus 的 SPIE 位元設為 1
   - 控制再返回 user mode 後是否啟用中斷 intr_on() / intr_off() 是設置 SIE 位元 但這裡不直接設置 SIE SIE：控制「當前模式」的中斷狀態 而是設置 SPIE SPIE：kernel 預設 user mode 的中斷狀態 因為當 之後 userret 執行 sret 指令返回 user mode 時 會將 SPIE 的值複製到 SIE 如果 SPIE 是 1，則返回 user mode 後中斷會被啟用 如果 SPIE 是 0，則返回 user mode 後中斷會被禁用 usertrapret() 裡面已經關閉了中斷 所以這裡設置 SPIE 為 1 這樣返回 user mode 後中斷就會被啟用 SSTATUS_SPIE 定義在 kernel/riscv.h 中
7. 設定返回 user mode 後的 program counter
8. 把用戶的 page table 地址轉換為 SATP 暫存器的格式，賦值給 satp 變數
   - 這個值會被傳遞給 userret 函數，userret會把這個值切換回用戶頁表
9. 呼叫 trampoline_userret 函數（參數是 satp)

### userret 函數處理
userret 函數：
1. 將用戶頁表設定到 satp 暫存器（前面傳遞的參數現在在 a0 register）
2. 將 trapframe 裡面的值還原到 register 中
3. sret 做三件事
   - 特權模式切換
   - sstatus.SIE = sstatus.SPIE
   - 程式跳轉


## Implementation - 1 trace

### hint 1
把 $U/_trace 加到 Makefile 中的 UPROGS
``` makefile
# hint 1: add trace to UPROGS
UPROGS=\
	$U/_cat\
	$U/_echo\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_grind\
	$U/_wc\
	$U/_zombie\
	$U/_trace\
```

### hint 2
擴充 user/usys.pl 不然會找不到 trace 的進入點
同時在 user/user.h 中宣告 trace
``` pl
# hint 2: add a stub
entry("trace");
```
``` c
# hint 2: add a prototype for trace
int trace(int);
```

### hint 3
在 kernel/sysproc.c 中新增 sys_trace 取得參數的方式要從 argint 取得並把這個參數放到 PCB 中 在 kernel/proc.h 中修改 proc struct 新增 trace_mask

### hint 4
新增一些檢查 例如 mask 小於 0 或是無法找到當前進程的 PCB

``` c
// hint 3: add sys_trace in sysproc.c
uint64
sys_trace(void)
{
  int mask;
  struct proc *p;

  // 從用戶空間獲取第一個參數 (mask)
  argint(0, &mask);

  if(mask < 0) {
    return -1; // 如果 mask 為負數，返回錯誤
  }

  p = myproc(); // 獲取當前進程的 PCB
  if(p == 0) {
    return -1; // 如果沒有當前進程，返回錯誤
  }

  p -> trace_mask = mask; // 設置當前進程的 trace_mask
  return 0; // 成功
}
```

### hint 5
在 kenrel/proc.c/fork 函數中新增把 parent 的 trace_mask 繼承給 new process 的 trace_mask
``` c
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

+  // hint 5: 將父進程 proc 的 trace_mask 複製到子進程 proc 的 trace_mask 
+  np -> trace_mask = p-> trace_mask;
  
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
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}
```

### hint 6
kernel/trap.c 中 usertrap 透過 syscall 呼叫如果呼叫 kernel/syscall.c 的 syscall 然後依照 kernel/syscall.c 中 syscalls\[num\] 指向的位址做一系列操作，結束後，要再去比較這個 syscall 是不是我們要追蹤的，如果是的話就要再去印出需要的資訊
在這邊因為印出的資訊要包含 syscall 的名稱，所以要先在 kernel/syscall.c 新增一個 syscall_names 的字串陣列

``` c
// hint 6: for get the syscall name
static char *syscall_names[] = {
  [SYS_fork]    "fork",
  [SYS_exit]    "exit",
  [SYS_wait]    "wait",
  [SYS_pipe]    "pipe",
  [SYS_read]    "read",
  [SYS_kill]    "kill",
  [SYS_exec]    "exec",
  [SYS_fstat]   "fstat",
  [SYS_chdir]   "chdir",
  [SYS_dup]     "dup",
  [SYS_getpid]  "getpid",
  [SYS_sbrk]    "sbrk",
  [SYS_sleep]   "sleep",
  [SYS_uptime]  "uptime",
  [SYS_open]    "open",
  [SYS_write]   "write",
  [SYS_mknod]   "mknod",
  [SYS_unlink]  "unlink",
  [SYS_link]    "link",
  [SYS_mkdir]   "mkdir",
  [SYS_close]   "close",
  [SYS_trace]   "trace",
  [SYS_sysinfo] "sysinfo"
};
```

``` c
void
syscall(void)
{
  int num;
  struct proc *p = myproc();
  // 在 user/usys.S 裡的 a7 儲存了系統調用號
  // 然後在 kernel/trampoline.S 中 uservec 會將 a7 的值保存到 trapframe 裡  
  // 他儲存的位址是 p->trapframe->a7（這是結構體的欄位名稱），不要跟暫存器名稱搞混
  num = p->trapframe->a7;
  // NELEM() 是一個巨集，計算陣列的元素數量
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Use num to lookup the system call function for num, call it,
    // and store its return value in p->trapframe->a0
    // return value 存在 p->trapframe->a0
    p->trapframe->a0 = syscalls[num]();

    // hint 6: if tracing is enabled for this syscall, print it
+    if(p -> trace_mask & (1 << num)) {
+      printf("%d: syscall %s -> %d\n", p -> pid, syscall_names[num], p -> trapframe -> a0);
+    }
+  } else {
+    printf("%d %s: unknown sys call %d\n",
+            p->pid, p->name, num);
+    p->trapframe->a0 = -1;
+  }
}
```
## Implementation - 2 sysinfo

### hint 1
把 $U/_trace 加到 Makefile 中的 UPROGS
``` makefile
# hint 1: add trace to UPROGS
UPROGS=\
	$U/_cat\
	$U/_echo\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_grind\
	$U/_wc\
	$U/_zombie\
	$U/_trace\
    $U/_sysinfotest\
```

### hint 2
在 user/user.h 中宣告 sysinfo() 的prototype (並注意先行宣告struct sysinfo)
``` c
//os25 modify code: add sysinfo
int sysinfo(struct sysinfo*);
```
``` c
struct sysinfo;
```

### hint 3
函式宣告問題在前兩個步驟解決，Compile成功，但實作部分尚未完成，以下為在kernel/sysproc.c中sysinfo()的實作說明

``` c
uint64
sys_sysinfo(void)
{
  struct sysinfo info;
  uint64 uaddr;   // user pointer
  // 取出使用者傳來的指標
  argaddr(0, &uaddr);
  // 填入 kernel 收集的資訊
  info.freemem = freeram();  //實作 freeram()
  info.nproc = proc_count(); // 實作 proc_count()
  // copy 到 user space
  struct proc *p = myproc();
  if (copyout(p->pagetable, uaddr, (char*)&info, sizeof(info)) < 0)
    return -1;
  return 0;
}
```
### hint 4
參考在kernel/sysfile.c 中 sys_fstat() 和kernel/file.c 中 filestat() 中copyout()的方法，收集到的資訊回傳到user space中

### hint 5
如果成功將資訊回傳到user space則return 0否則 return -1

``` c
if (copyout(p->pagetable, uaddr, (char*)&info, sizeof(info)) < 0)
    return -1; 
return 0;
```

### hint 6
確認sysinfo可以被trace

### hint 7
在 kernel/sysproc.c 中實作 freeram 函式用來取得system中有多少freemem(byte)
``` c
uint64
freeram(void)
{
    struct run *r; //用來遍歷 kmem.freelist 的鏈表
    uint64 count = 0; //用來計算目前有多少頁
    //lock
    acquire(&kmem.lock);      // 保護 freelist
    //遍歷 kmem.freelist
    for(r = kmem.freelist; r != 0; r = r->next)
        count++;
    release(&kmem.lock);
    return count * PGSIZE;    // 換算成 byte 並回傳
}
```

### hint 8
在 kernel/proc.c中實作proc_count()函式用來取得system中有多少processes != UNUSED
``` c
uint64
proc_count(void)
{
    struct proc *p;
    int cnt = 0;


    // 保護 process table 避免 race condition
    //acquire(&p.lock); 目前不加lock 大部分情況不會有race condition
    for(p = proc; p < &proc[NPROC]; p++){
        if(p->state != UNUSED)
            cnt++;
    }
    //release(&p.lock);
    return cnt;
}
```

### hint 9
在kernel/defs.h中宣告兩個實作完的函式
``` c
uint64          freeram(void); //新的函式宣告: 用來取得freemem
uint64          proc_count(void); //新的函式宣告: 用來取得 # proc whose state != UNUSED
```