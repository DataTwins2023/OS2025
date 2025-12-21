# Part 1
## Trace Code
### Part A: Writing to a Large File

按照情境的敘述，我們從 userspace 的 `write()` 開始
1. kernel/sysfile.c/sys_write()

    - 如同在 MP1 中 trace 的過程，在 user 呼叫 `write()` 時，會呼叫到 `user/usys.S` 中的 write 片段
        ```c
        .global write
        write:
        li a7, SYS_write
        ecall
        ret
        ```
    - 執行 `ecall` 指令，進入 kernel mode 並且跳到 `kernel/trap.c` 中的 `usertrap()` ，接著呼叫 `syscall()`
        ```c
        void
        usertrap(void)
        {
            ...
            if(r_scause() == 8){
                ...
                syscall();
            } else if((which_dev = devintr()) != 0){
                // ok
            } else {
            ...
            }
            ...
        }
        ```
    - `syscall()` 中會呼叫 `sys_write()` （在 `kernel/sysfile.c` 中）。
        ```c
        uint64
        sys_write(void)
        {
            struct file *f;
            int n;
            uint64 p;
            
            argaddr(1, &p);
            argint(2, &n);
            if(argfd(0, 0, &f) < 0)
                return -1;

            return filewrite(f, p, n);
        }
        ```
        函式中會：
        - 透過 `argaddr` 以及 `argint` 取得緩衝區的位址以及想要寫入的長度
        - 透過 `argfd(0, 0, &f)`
        ```c
        static int
        argfd(int n, int *pfd, struct file **pf)
        {
            int fd;
            struct file *f;

            argint(n, &fd);
            if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
                return -1;
            if(pfd)
                *pfd = fd;
            if(pf)
                *pf = f;
            return 0;
        }
        ```
        - 透過 `argint` 把第 0 個參數存到 `fd` 中
        - 檢查 fd 是否在有效範圍內且該 fd 必須有對應到的 open file
        - `pfd`: pointer to file descriptor
            
            如果不是 NULL，會把 file descriptor 的值寫到這個 pointer
        - `pf`: pointer to file pointer
            
            如果不是 NULL，會把 file structure 的指標寫到這個 pointer
        - `argfd(0, 0, &f)` 是要透過第 0 個參數 `fd` 取得 file structure 指標
        - 呼叫 `filewrite(f, p, n)`（在 `kernel/file.c` 中）

2. kernel/file.c/filewrite()
    
    這邊是 `filewrite(f, p, n)`，會做：
    - 先透過 struct file 檢查是否 writable，否則回傳 -1
    - 確認 struct file 的 `type`（定義在 `kernel/file.h` 中）
        - `FD_PIPE`
            - Pipe 是一種 IPC 機制
            - `pipewrite` 實作在 `kernel/pipe.c` 中
                ```c
                int
                pipewrite(struct pipe *pi, uint64 addr, int n)
                {
                    int i = 0;
                    struct proc *pr = myproc();

                    acquire(&pi->lock);
                    while(i < n){
                        if(pi->readopen == 0 || killed(pr)){
                            release(&pi->lock);
                            return -1;
                        }
                        if(pi->nwrite == pi->nread + PIPESIZE){ //DOC: pipewrite-full
                            wakeup(&pi->nread);
                            sleep(&pi->nwrite, &pi->lock);  
                        } else {
                            char ch;
                            if(copyin(pr->pagetable, &ch, addr + i, 1) == -1)
                                break;
                            pi->data[pi->nwrite++ % PIPESIZE] = ch;
                            i++;
                        }
                    }
                    wakeup(&pi->nread);
                    release(&pi->lock);

                    return i;
                }
                ```
                - 會先嘗試取得 lock
                - 逐一把字節寫入到 pipe 中，過程會
                    - 檢查 pipe 是否滿了
                        - 如果滿了，會去 `wakeup` 所有在等待 read 的 process，並且目前在寫入的 process 進入 `sleep`
                        - 沒滿的話，透過 `copyin` 從 user space 複製一個 ch 到 kernel space
                            - 如果失敗的話回傳 -1
                            - 成功的話就寫到 pipe 中
                - 循環結束， `wakeup` 正在等待 read 的 process
                - 釋放鎖
        - `FD_DEVICE`
            - 代表目前的 file descriptor 指向的是一個 「設備檔案」，接下來的邏輯會
                - 檢查設備的合法性（f -> major），沒的話 return -1
                - 檢查 kernel 的 `devsw` (在 `kernel/file.h`) 中有沒有定義這個設備的寫入處理函式，沒的話 return -1
                - 沒問題的話，透過 `devsw` 執行該設備的寫入函式，參數：
                    - user_src = 1 表示數據來源是 user space
                    - addr 是使用者緩衝區的地址以及長度 n
        - `FD_INODE`
            - 代表目前的 file descriptor 指向的是一個普通檔案，並且由 inode（這應該是記憶體中的副本） 管理
            - 首先會透過
                ```c
                int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
                ```
                確保每次寫入造成的修改不超過一定值日誌區塊的大小，否則日誌系統會崩潰
                - `MAXOPBLOCKS` 代表日誌系統的最大區塊容量（10，定義在 `kernel/param.h`）
                - `-1` 預留給 `Log Header` 區塊
                - `-1` 預留給 `inode` 區塊（雖然一次的寫入只會需要修改一個 inode，但日誌系統以 block 為單位記錄修改，所以需要記錄整個 inode block）
                - `-2` 預留給 bitmap 及 directory block 的變動。當檔案跨越邊界或需要分配新塊時，這些 metadata 會被修改
                - `/2` 這是為了處理 「非對齊寫入 (Non-aligned write)」
                - `* BSIZE` 轉換單位為 Byte
            - 開始寫入，透過 while loop 確認是否還有數據要寫入
                - 如果剩餘量 (n1) 大於單次日誌事務能安全處理的最大長度，則將本次寫入的長度限制在 max
                - begin_op 是日誌系統宣告原子操作開始
                - ilock(f -> ip) 取得 `inode` 的鎖，讓目前的 process 可以修改檔案的數據跟 metadata（因為 file 在開啟時，已經透過 `sys_open` 把 inode 載入到 memory 了）
                - `writei`（實作在 `kernel/fs.c`） 下一個階段會說明
                - iunlock(f -> ip) 釋放 `inode` 的鎖
                - end_op 通知日誌系統這次的操作已經完成，必且會檢查如果沒有其他事物在進行就會把所有暫存的變更寫到 disk
                - if(r != n1) 
                    - 成立的話代表實際寫入的數量小於原本預定的 `n1` ，表示發生錯誤，那就中斷 while loop
                    - 不成立那就更新進度
            - while loop 結束，如果都有成功寫入的話就返回長度 `n`，否則返回 -1
        - 以上皆非： panic

3. kernel/fs.c/writei() 前半段
    
    - writei 的參數如下
        - struct inode *ip：指向要寫入檔案的 inode 結構體
        - int user_src：這是一個布林值，如果 1 代表數據來源是 user space，否則就是 kernel space
        - uint64 src：要寫入的數據的起始記憶體位址
        - uint off：在檔案內的哪一個偏移量開始寫入
        - uint n：寫入的長度

    - 開頭的兩個 if 在檢查
        - 偏移量是否超過檔案的實際大小以及是否有 overflow 出現
        - 確保這次的寫入不會造成檔案超出原始設計的最大上限

    - 接下來進入到 for 迴圈
        - 首先初始化 `tot`（total），條件是只要 `tot` < n 就繼續，每次結束後 `tot`、 `off` 以及 `src` 都會增加 m
        - 呼叫 `bmap` 函數（定義在 `kernel/fs.c` 中）
        
4. kernel/fs.c/bmap()
    - 透過 struct inode 指標和 offset 來計算 logical block 並轉成對應的 physical block，會做：
        ```c
        static uint
        bmap(struct inode *ip, uint bn)
        {
            uint addr, *a;
            struct buf *bp;

            if(bn < NDIRECT){
                if((addr = ip->addrs[bn]) == 0){
                    addr = balloc(ip->dev);
                    if(addr == 0)
                        return 0;
                    ip->addrs[bn] = addr; // ip->addrs 代表該檔案第 n 個 block 在磁碟上的哪個 block，這邊修改的是 indoe 而 inode 的同步不由 bmap 負責，因為修改的 inode 是在記憶體中，所以同步的事情可以等到 write 要結束再透過 iupdate 做
                }
                return addr;
            }
            bn -= NDIRECT;

            if(bn < NINDIRECT){
                // Load indirect block, allocating if necessary.
                if((addr = ip->addrs[NDIRECT]) == 0){
                    addr = balloc(ip->dev); // 分配給 NINDIRECT 索引的 disk
                    if(addr == 0)
                        return 0;
                    ip->addrs[NDIRECT] = addr; // 針對 inode 本身的修改
                }
                bp = bread(ip->dev, addr); // 讀取剛剛分配給 NINDIRECT 索引的 disk 區塊
                a = (uint*)bp->data;
                if((addr = a[bn]) == 0){
                    addr = balloc(ip->dev);
                    if(addr){
                        a[bn] = addr; // 修改 block 內容（來自 disk）
                        log_write(bp); // 將對 buffer cache 的修改寫到 log 中
                        // 需要立即寫回是因為 buffer cache 可能被回收
                    }
                }
                brelse(bp);
                return addr;
            }

            panic("bmap: out of range");
        }
        ```
        - `NDIRECT` 定義在 `kernel/fs.h` 中，值是 12。如果要求的 `bn` 小於 12，代表這是「直接區塊」
            - 透過 addr = ip->addrs[bn]，檢查這個區塊是否已經被分配
                - 如果是 0 代表這個邏輯區塊在 disk 上還沒分配任何實體空間
                    - 透過 `balloc` （同樣定義在 `kernel/fs.c` 中）分配一個空閒的磁碟區塊
                        ```c
                        static uint
                        balloc(uint dev)
                        {
                            int b, bi, m;
                            struct buf *bp;

                            bp = 0;
                            for(b = 0; b < sb.size; b += BPB){
                            // 這是要找 bitmap block
                                bp = bread(dev, BBLOCK(b, sb));
                                for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
                                    // 從 bitmap 裡面確認 block 是否 free
                                    m = 1 << (bi % 8);
                                    if((bp->data[bi/8] & m) == 0){  // Is block free?
                                        bp->data[bi/8] |= m;  // Mark block in use.
                                        log_write(bp); // 將對 bitmap 修改寫到 log 中
                                        brelse(bp); // 釋放 buffer
                                        bzero(dev, b + bi); // 將找到的 disk block 內容寫成 0
                                        return b + bi;
                                    }
                                }
                                brelse(bp);
                            }
                            printf("balloc: out of blocks\n");
                            return 0;
                        }
                        ```
                        - 外層 for 迴圈
                            - `sb` 是 superblock，`sb.size` 是紀錄 file system 的總 block 數量
                            - `BPB` 是 bits per block 定義在 `kernel/fs.h` 中
                            - 每次都是增加一個 bits per block 的大小（8192）
                            - `bread`（buffer read，定義在 `kernel/bio.c` 中），目的是讀取一個 block （這邊是 bitmap block）到記憶體中的 buffer，這樣才能修改，做的事情是：
                                - 透過 `bget` 取得或分配 buffer
                                    - 檢查 buffer cache 是否已經有這個 block
                                        - 有的話就回傳這個 buffer（要增加 b -> refcnt）
                                        - 沒的話透過 LRU 選出一個沒在用的 buffer 並回傳
                                - 檢查 buffer 的有效性
                                    - valid = 0 代表 buffer 是空的
                                        - 透過 `virtio_disk_rw` 從 disk 讀取資料到 buffer
                                        - 把 valid 標記為 1
                                    - valid = 1 代表 buffer 已經有資料
                                - BBLOCK` 定義在 `kernel/fs.h`         
                                    ```c
                                    #define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)
                                    ```
                                    目的是要計算管理某個 block 的 bitmap block 號碼
                            - 內層 for 迴圈
                                - 針對每個透過 bread 讀取到記憶體 buffer 的 bitmap block 透過 bi / 8 計算是第幾個 byte 然後再透過 m 作為 mask 去判斷該 block 是否空閒
                                    - 如果空閒，將其標注為 `use`
                                    - 將修改寫到 log 中
                                    - 釋放 buffer，這是在 `bread` 中呼叫 `bget` 時取得的
                                    - 將新分配的 block 全部寫成 0
                                    - 並且 return 該 block 的號碼
                                - 全部跑完找不到空閒，回傳 0
                    - 如果 `balloc` 回傳 0，代表找不到 disk 區塊， "out of blocks"
                    - 否則把新得到的區塊號碼記在 inode 中
                    - 回傳新得到的區塊號碼
                    - 總結來說
                        - `balloc`：全名是 Block Allocate。要在 disk 上找一個沒在使用的 block ，並使用它
                        - `bread`：全名是 Buffer Read。從磁碟讀取一個 block 到記憶體中。
            - 如果已經被分配那就回傳 `addr`
        - 先轉換為間接區塊內的相對索引 （bn -= NDIRECT;）
            - 同樣先透過 addr = ip->addrs[INDIRECT]，檢查這個間接區塊是否已經被分配
                - 如果沒被分配，就和上面一樣透過 `balloc` 在磁碟上找一個空閒的 block 給檔案使用，透過掃描 bitmap 找到空閒位置後標記為使用中，並回傳 block 號碼
                - 不論是否需要分配，在完成都要做：
                    - 透過 `bread` 把間接區塊讀到記憶體
                    - 間接區塊會被看作是一個「指標陣列」
                    - 接著去查看這個指標陣列的第 bn 個位置（上面有先做轉換的索引），如果是 0，代表還沒被分配 block，一樣要透過 balloc 分配 block
                    - 分配成功，記錄間接區塊中並透過 `log_write` 寫回 disk（這是針對索引區塊的修改）
                    - 釋放 buffer
                    - 回傳 block 號碼

5. kernel/fs.c/writei() 後半段
    - `bmap` 回傳實體 block 號碼
    - 如果是 0 就 break
    - 透過 `bread` 把 `bmap` 回傳的實體 block 讀到記憶體中的 buffer
    - m 是要計算這次要寫入的 bytes 數量
        - 就是比較還有多少要寫 以及 這個 block 還能寫多少，取小的
    - 透過 `either_copyin` (定義在 `kernel/proc.c` 中) 把資料從 user space 複製到 kernel space（因為 `kernel/file.c` 中 `writei` 的第二個參數是 1 所以知道來源是 user space）
        - 如果有問題，釋放 `bp` 鎖（這是鎖 bread 拿到的 buffer）並 break
        - 否則把修改寫入 log 並釋放 buffer（因為已經用完這次 buffer 了）
    - 如果寫入後， offset 超過原本的檔案大小，那就要更新
    - 如註解所寫就算檔案大小沒變，還是要把 inode 寫回 disk，因為 `bmap()` 可能改變了 ip -> addrs[]
        ```c
          if(bn < NDIRECT){
            if((addr = ip->addrs[bn]) == 0){
            addr = balloc(ip->dev);
            if(addr == 0)
                return 0;
            + ip->addrs[bn] = addr; // 將新分配的區塊指派給 inode
            }
            return addr;
        }
        bn -= NDIRECT;
        ```

    
### Part B: Deleting a Large File
1. kernel/sysfile.c/sys_unlink()
    - 如同在 Part A，在 user 呼叫 `unlink()` 時，會呼叫到 `user/usys.S` 中的 unlink 片段
        ```c
        .global unlink
        unlink:
        li a7, SYS_unlink
        ecall
        ret
        ```
    - 執行 `ecall` 指令，進入 kernel mode 並且跳到 `kernel/trap.c` 中的 `usertrap()` ，接著呼叫 `syscall()`
        ```c
        void
        usertrap(void)
        {
            ...
            if(r_scause() == 8){
                ...
                syscall();
            } else if((which_dev = devintr()) != 0){
                // ok
            } else {
            ...
            }
            ...
        }
        ```
    - `syscall()` 中會呼叫 `sys_unlink()` （在 `kernel/sysfile.c` 中）。
        ```c
        uint64
        sys_unlink(void)
        {
            struct inode *ip, *dp; // ip 是想要刪除的 inode， dp 是 parent directory 的 inode
            struct dirent de; // dirent: directory entry，用於清空目錄條目的緩衝區
            char name[DIRSIZ], path[MAXPATH]; // 儲存檔案名稱以及完整路徑 MAXPATH 定義在 `kernel/param.h`
            uint off; // 紀錄該檔案在 parent directory 的 offset

            // 從 systemcall 參數取得要刪除的檔案路徑
            if(argstr(0, path, MAXPATH) < 0)
                return -1;

            // transition 開始
            begin_op();
            // 取得父目錄的 inode 和檔案名稱
            if((dp = nameiparent(path, name)) == 0){
                end_op();
                return -1;
            }

            // 把父目錄 lock 起來
            ilock(dp);

            // Cannot unlink "." or "..".
            // 不能刪除當前目錄 或 父目錄
            if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
                goto bad;

            // 在父目錄找要刪除的檔案
            // dirlookup 會把找到的 inode 存到 ip
            // 然後檔案在目錄裡面的 offset 存到 off
            if((ip = dirlookup(dp, name, &off)) == 0)
                goto bad;

            // 把要刪除的檔案 lock 起來
            ilock(ip);

            // 早就沒有 hard link 了
            if(ip->nlink < 1)
                panic("unlink: nlink < 1");

            // 如果是目錄，要檢查是否為空
            // 只能刪除空目錄
            if(ip->type == T_DIR && !isdirempty(ip)){
                iunlockput(ip);
                goto bad;
            }

            // 在父目錄中刪除這個 directory entry
            // 先把 de 全部的資料都設為 0
            memset(&de, 0, sizeof(de));
            // 在 off 的位置（dirlookup 有存起來）寫入空的 dirent 來覆蓋原本的目錄項
            if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
                panic("unlink: writei");
            // 如果刪除的是目錄，父目錄的 nlink 要減 1
            if(ip->type == T_DIR){
                dp->nlink--;
                iupdate(dp);
            }
            // 釋放父目錄
            iunlockput(dp);

            // 減少被刪除檔案的 nlink
            ip->nlink--;
            // 寫回檔案的 inode
            iupdate(ip);
            // 釋放檔案 inode
            // 如果 nlink 變成 0，iput 會自動釋放檔案佔用的 blocks
            iunlockput(ip);

            end_op();

            return 0;

        bad:
            // 解鎖父目錄並結束 transaction
            iunlockput(dp);
            end_op();
            return -1;
        }
        ```
        - 先透過 `argstr` 實作在 `kernel/syscall.c` 取得要刪除的檔案路徑
        - 透過 `begin_op()` 來開始操作
        - 透過 `nameiparent` （實作在 `kernel/fs.c`），實際上是呼叫 `namex`，做：
            - 決定透過絕對路徑、相對路徑開始尋找
            - 使用 `skipelem`，每次取出路徑中的下一個目錄或檔案名稱，並更新 path 指向剩餘的路徑部分
            - 如果 nameiparent 有被設置（代表是要查找父目錄），那當 *path == '\0' 就可以停止，代表路徑解析程序成功地找到了目標檔案名稱的父目錄 inode，並準備提早停止，返回 inode（此時，ip 就是目標組件的父目錄 inode）
            - 透過 `dirlookup` 在當前目錄尋找下一個目錄或檔案名稱對應到的 node
            - 如果一路到了最終點，但模式是父目錄查找，代表失敗，因為路徑中沒有額外的子組件可供操作（例如路徑只是 /、. 或 ..），因此無法找到一個可供 link 或 unlink 操作的「父目錄-子名稱」組合。 在這種情況下，函式會釋放對起始 inode 的引用 (iput(ip))，並返回 0
        - `nameiparent` 總結就是要回傳給訂路徑的父目錄 inode（`dp`），同時將目標檔案名稱複製到指定的緩衝區（`name`）中
        - 取得父目錄的鎖
        - 檢查目標名稱是不是目錄結構中強制存在的 `.` or `..`。是的話就應該被禁止， goto bad
        - 透過 `dirlookup` （實作在 `kernel/fs.c` 中）在給定的父目錄中，搜尋特定的檔案或目錄名稱，然後返回 inode (ip)，並且可以返回尋找的目標在目錄中的 offset，找不到就 goto bad
        - 取得要刪除檔案、目錄的鎖
        - 檢查 inode 的 nlink，如果小於 1 代表它理論上已經被釋放，這應該是 bug 導致的，產生 `panic`
        - 不能刪除空目錄，做 iunlockput(ip)，並 goto bad
            - 做 `iunlock`
            - 做 `iput`，主要目的是減少 ip -> ref。因為 `sys_unlink` 已經結束對這個 indoe 的使用

        - 先把 de 全部的資料都設為 0
        - 透過 writei 去覆蓋父目錄中對應目標檔案名稱的 directory entry，有錯誤就 panic
        - 如果要刪除的是目錄，記得父目錄的 `nlink` 要減一（如果是檔案，他不會去指向父目錄來增加父目錄 `nlink` ，所以不需要減），接著就可以把父目錄的更新資訊寫回到 disk 中
        - iunlockput(dp)
        - 減少 inode 的 `nlink` 計數，將目標更新資訊寫回到 disk
        - iunlockput(ip)  
        - bad 要做
            - iunlockput(dp)
            - 結束操作
            - 回傳 -1
    - kernel/fs.c/iunlockput()/iput()
        - iunlockput 會呼叫 `iunlock` 跟 `iput`
        - 其中 `iput`
            - 主要處理 inode 引用釋放以及資源清理
                - 取得 `itable` （定義在 `kernel/fs.c`，負責存放 inode struct）的 lock
                - 第一個判斷：
                    - inode 的 ref 是 1 且 inode 的 metadata 還有效以及 inode 的硬連結計數為 0
                    
                    **ip -> ref 和 ip -> nlink 有什麼不同？**

                    **ip -> ref 代表內存中有多少個 kernel data structure 正在使用這個 inode 副本。 ip -> ref 等於 1 代表只有目前正在執行 iput 的 process 有對這個 inode 做引用**

                    **ip -> nlink 代表 disk 上有多少個檔案指向這個 inode**

                    所以當這以上條件成立，那代表這個檔案可以進行永久刪除了，會做以下操作
                    - `acquiresleep` 在清理前先鎖定 inode
                    - 釋放 `itable` 的 lock
                    - 透過 `itrunc(ip)` 來釋放 inode 所指向的所有 block 並重設檔案大小為 0
                    - 把 type 設為 0（空閒），然後寫回 disk 的日誌區
                    - 把 valid 標記為無效
                    
                      **都已經 iupdate 了，為什麼還要修改 valid ，這是因為 iupdate 是把已經在 mem 中清空的 ip 寫回到 disk 上的 inode 區域。但在 mem 上，這塊 inode 仍存在，所以設定 valid = 0，等於是讓之後要使用的人知道，這塊記憶體區塊，現在跟 disk 沒有關係，要從 disk 中重新讀取**

                    - `releasesleep` 釋放鎖
                    - 重新鎖定 `itable`
                - ip -> ref --;
                - 釋放 `itable` 的鎖
        - 所以 iunlockput 就是在 unlock inode 並減少 inode 的 reference 計數，接著允許其他 process 使用或是做資源的清理

            **sys_unlink 不會每次都清空（釋放 blocks 和標記 inode 為未使用）檔案的 Inode。只有在兩個條件滿足下**
            - 硬連結計數為 0
            - reference 只剩下一個
            
            才會被 iput 呼叫 itrunc 徹底清理
    - kernel/fs.c/itrunc()
        - 目的就是清理並釋放某個檔案原本佔有的 block
        - 首先釋放 direct blocks
            ```c
            for(i = 0; i < NDIRECT; i++){
                if(ip->addrs[i]){
                    bfree(ip->dev, ip->addrs[i]);
                    ip->addrs[i] = 0;
                }
            }
            ```
            `bfree` 在 `kernel/fs.c` 中，目的就是要釋放一個 disk block
        - 釋放 indirect blocks
            ```c
            if(ip->addrs[NDIRECT]){
                bp = bread(ip->dev, ip->addrs[NDIRECT]);
                a = (uint*)bp->data;
                for(j = 0; j < NINDIRECT; j++){
                    if(a[j])
                        bfree(ip->dev, a[j]);
                }
                brelse(bp);
                bfree(ip->dev, ip->addrs[NDIRECT]);
                ip->addrs[NDIRECT] = 0;
            }
            ```
            同樣的，要先把間接區塊讀到記憶體中的緩衝區，才能去讀裡面的東西，接著遍歷所有 NINDIRECT ，只要非零就呼叫 `bfree` 來釋放（內部會有 `log_wrtie`）。

            接著再釋放間接區塊本身佔用的 disk 區塊

            透過 ip -> addrs[NDIRECT] = 0; 標注這個空間為空閒
        - 更新檔案大小為 0
        - 寫回 disk

### Question:
1. What is the current maximum file size xv6 can support? How many numbers of direct and indirect blocks are there?

    目前 xv6 支援最大的檔案大小可以透過 inode struct （定義在 `kernel/fs.h`）中的 `addrs` 推算
    ```c
    // On-disk inode structure
    #define NDIRECT 12
    struct dinode {
        short type;           // File type
        short major;          // Major device number (T_DEVICE only)
        short minor;          // Minor device number (T_DEVICE only)
        short nlink;          // Number of links to inode in file system
        uint size;            // Size of file (bytes)
        uint addrs[NDIRECT+1];   // Data block addresses
    };
    ```
    可以知道總共有 13 個區塊，其中有 12 個是 `DIRECT` 還有 1 個是 `INDIRECT`
    
    `DIRECT` 區塊可以佔到 12 * 1024 個 Bytes，而 `INDIRECT` 中的指標數量是 BSIZE/sizeof(uint) = 256 個，因此總共可以佔到 256 * 1024 個 Bytes。
    
    兩者相加後事 274,432 個 Bytes
2. How many blocks increase when replacing a direct block with a doubly-indirect block?

    如果用一個 `doubly-indirecet block` 去替換一個 `direct block`。那首先會減少一個直接區塊（-1），接著這個 `doubly-indirect block` 會替檔案增加 256 * 256 個區塊。

    所以總體來說是 65,536 - 1 = 65,535
3. The `bmap` and `itrunc` functions make use of the buffer cache via `bread`, `brelse`, and `log_write`. Explain the above function .
    - bmap：實作在 `kernel/fs.c`，目的是要把檔案的 logic block num 轉換為 disk 的 physical block num
        - 輸入是
            - ip：inode 指標
            - bn：檔案內的 logical block num
        - 過程
            - 如果是 `direct block`（bn < 12）
                - 直接從 ip -> addrs[bn] 讀取
                - 如果 ip -> addrs[bn] == 0 代表尚未分配
                    - 呼叫 `balloc(ip->dev)` 分配新的 block
                    - 將 `balloc`return 的 block number 放到 `ip->addrs[bn]`
                    - 這邊修改的是記憶體中的 inode 副本，之後會透過 `iupdate()` 同步到 disk
            - 如果是 `indirect block`（bn >= 12）：先讀取間接區塊，再查詢對應的指標
                - step 1：檢查 indirect block 本身
                    - 檢查 `ip->addrs[NDIRECT]`
                    - 如果是 0 就要呼叫 `balloc()` 分配 indirect block，並存入 `ip->addrs[NDIRECT]`
                - step 2：處理 indirect block 的內容
                    - 用 `bread()` 把 indirect block 讀到 buffer cache
                    - 檢查 `a[bn]` （第 bn 個 data block 的編號）
                        - 如果是 0：呼叫 `balloc()` 分配 data block 並存入 `a[bn]`
                    - 修改 indirect block 的內容後，必須呼叫 `log_write(bp)` 立即寫回
                    - 用 `brelse(bp)`  釋放 buffer
            - 超出範圍的話就會出現 `panic`
    - itrunc：實作在 `kernel/fs.c`，主要目的是釋放 inode 佔用的所有區塊並且將檔案大小設為 0
        - 輸入是
            - ip：inode 指標（要清空的）
        - 過程
            - 釋放所有 direct block
                - 遍歷 `ip->addrs[0]` 到 `ip->addrs[11]`
                - 如果 `ip->addrs[i] != 0`：
                    - 呼叫 `bfree(ip->dev, ip->addrs[i])` 釋放該 block
                    - 將 `ip->addrs[i]` 設為 0  
            - 釋放間接區塊（分為所有間接指向的資料區塊以及間接區塊本身）
                - 如果 `ip->addrs[NDIRECT] != 0`（有 indirect block）：
                    - 用 `bread()` 讀取 indirect block 到 buffer
                    - 把 `bp->data` 解釋為 `uint` 陣列
                    - **先釋放所有 data blocks**：
                        - 遍歷陣列中的每個 `a[j]`
                        - 如果 `a[j] != 0`：呼叫 `bfree(ip->dev, a[j])`
                    - 用 `brelse(bp)` 釋放 buffer
                    - **再釋放 indirect block 本身**：
                        - 呼叫 `bfree(ip->dev, ip->addrs[NDIRECT])`
                        - 將 `ip->addrs[NDIRECT]` 設為 0
            - **更新 inode metadata**
                - 將 ip -> size 設為 0
                - 呼叫 `iupdate(ip)` 將修改同步到磁碟
    - bread：實作在 `kernel/bio.c`，主要目的是讀取一個 block 到 memory 中的 buffer cache。
        - 輸入是
            - dev：裝置編號
            - blockno：要讀取的 block 號碼
        - 過程
            - 透過 `bget()` 來取得或分配 buffer，`bget()` 會：
                - 先在 buffer cache 中尋找是否已存在（cache hit）
                - 如果不存在，分配一個 LRU 的 buffer
                - 鎖定並返回 buffer
            - 檢查 buffer 的 valid（這在 `bget` 中會去設定）
                - `valid == 1`：buffer 中資料有效，直接返回
                - `valid == 0`：buffer 中資料無效，要從 disk 讀取
                    - 透過 `virtio_disk_rw(b, 0)` 從磁碟讀入資料
                    - 將 valid 標記為 1
            - 返回 buffer
        - 補充
            - 在 `bread` 中一開始會透過 `bget` 來鎖著整個 cache 並找到需要的 buffer cache，接著就會解鎖整個 cache 但還是鎖住單一需要的 buffer cache
        - 輸出：指向 buffer 的 pointer
    - brelse：實作在 `kernel/bio.c`，目的是要釋放 buffer 並減少 ref count
        - 輸入是
            - b：要釋放的 buffer cahe 指標
        - 過程
            - 減少 buffer 的 `refcnt`
            - 如果 `refcnt` == 0，將 buffer 移到 LRU 列表的頭部
            - 透過 `releasesleep` 喚醒等待這個 buffer 的 process（可能是兩個 process 要讀同一個 block）
        - 補充
            - `brelse` 一開始會釋放單一 buffer cache 的鎖
            - 接著鎖住整個 cache 來做 refcnt 的調整
    - log_write：實作在 `kernel/log.c`，主要目的是標記 buffer 需要寫回 disk，並加入 transaction log
        - 輸入是
            - b：已修改的 buffer
        - 前提
            - 必須在 `begin_op()` 和 `end_op()` 之間呼叫
        - 過程
            - 檢查是不是在 transaction 中（透過 `log.outstanding`）
            - 檢查 log 空間是否足夠（`log.lh.n` 是目前的 transaction 已經紀錄多少個 blocks）
            - 透過掃描已經紀錄的 blocks 檢查是否已經紀錄過這個 block，這樣做的原因是因為同一個 block 可能被修改多次，但 log 不用重複紀錄，只要覆蓋就好 
            - 接下來會有兩種情況
                - 已經紀錄過的 block，其實沒變
                - 第一次紀錄的 block
                    - 將 `b->blockno` 加入 `log.lh.block[log.lh.n]`
                    - `log.lh.n++`（記錄的 block 數量加一）
                    - 透過 `bpin` 來增加 ref count，是要防止這個 buffer 在 transaction 結束前被踢出 cache
4. Explain the importance of calling `brelse` after you are done with a buffer from `bread`.

    可以從 `brelse` 的過程來回答這個問題
    - 釋放鎖定：當 process 呼叫 `bread` 時內部會呼叫 `bget`，而 `bget` 會取得取得 buffer 的 block
    ```c
    static struct buf*
    bget(uint dev, uint blockno)
    {
        struct buf *b;

        acquire(&bcache.lock);

        // Is the block already cached?
        for(b = bcache.head.next; b != &bcache.head; b = b->next){
            if(b->dev == dev && b->blockno == blockno){
            b->refcnt++;
            release(&bcache.lock);
            + acquiresleep(&b->lock);
            return b;
            }
        }
    ...
    ```
    所以如果 `bread` 後沒有呼叫 `brelse` 那就會導致 buffer 沒有得到釋放，其他 process 無法操作
    - 減少 `refcnt`，當 `refcnt` 歸零，這個緩衝區就被當作未使用，那就要重新放到 buffer cache 的最前端，這樣之後 `bget` 才能選到沒在使用的 buffer，所以如果沒有呼叫 `brelse` 的話，沒在使用的 buffer 也會是「忙碌」，最後可能沒有 buffer 能用
5. To ensure crash safety, xv6 uses a write-ahead log. Trace the lifecycle of a file system transaction, for example when creating a new file via `create()` in `kernel/fs.c`. Explain the roles of `begin_op()`, `log_write()`, and `end_op()` from `kernel/log.c`. Describe what gets written to the on-disk log and what constitutes a "commit". Finally, explain how the recovery code (`recover_from_log`) uses the log to ensure the operation is atomic after a crash. We strongly encourage you to read the logging part(8.4) in xv6 handbook to get familiar with loggin mechanism.
    - begin_op()：標記一個原子操作的開始
        - 過程會
            - 先取得 `struct log` 的鎖，防止大家同時修改
            - while loop
                - 檢查系統是否正在提交，如果正在提交，呼叫 `sleep(&log, &log.lock)`，釋放鎖並進入睡眠狀態，等提交完成後被喚醒
                - 檢查日誌空間是否足夠，主要是計算目前已經佔用的空間加上本次操作加上所有正在進行的操作所需的最大預留空間是否超過 LOGSIZE。如果空間不足，同樣呼叫 `sleep(&log, &log.lock)`
                - 前面檢查都通過的話，增加 `log.outstanding`
                - 釋放鎖並退出 while 循環
        - 確保以下兩點 process 才會繼續執行
            - 系統目前沒有在提交，不然就要等待
            - 這次操作加上所有在進行中的操作，需要的 disk 區塊不會超過 log 的容量
    - log_write()：將對 disk block 的內容修改，註冊到目前的 transaction 中
        - 過程會
            - 先取得 `log` 的鎖
            - 檢查 log 空間是否足夠（`log.lh.n` 是目前的 transaction 已經紀錄多少個 blocks）
            - 確定 `log_write` 是在 `begin_op` 及 `end_op` 間被呼叫的
            - 做 `log absorption` 會檢查該區塊是不是已經被目前的事物紀錄過。如果一個區塊在同一個事物中被修改很多次，那只需要紀錄最終的修改結果
               
                **log.lh 是 RAM 中用來追蹤目前未提交 transaction metadata**

                **log.lh.n 就是內存中日誌頭的區塊計數**
            - 如果 block 是第一次被記錄到，那就呼叫 `bpin(b)` 增加這個緩衝區在 buffer cache 內的計數，防止它在提交前被換出去
    - end_op()：目的是管理 transaction 的最後流程，並且在條件成立下進行 commit
        - 過程會
            - 取得 `log` 鎖
            - 減少 `log.outstanding`
            - 決定是否觸發 `commit`
                - 確認沒有 `log.committing`，防止嵌套 committ
                - 如果 `log.outstanding == 0` 表示目前沒有其他 process 在做 file system 的修改，設定 `do_commit` 跟 `log.committing`
                - 如果未歸零，則 `end_op` 會喚醒任何可能在 `begin_op` 中等待日誌空間的 process，然後退出
            - 釋放 `log` 鎖
            - 如果有設定 `do_commit` 就執行 `commit()` （實作在 `kernel/log.c`）
            - `commit()` 又分為四個階段
                - write_log()：把所有被修改的緩衝區內容複製並寫入到“磁碟上”的日誌區
                    ```c
                    static void
                    write_log(void)
                    {
                        int tail;

                        for (tail = 0; tail < log.lh.n; tail++) {
                            struct buf *to = bread(log.dev, log.start+tail+1); // log block
                            struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
                            memmove(to->data, from->data, BSIZE);
                            bwrite(to);  // write the log
                            brelse(from);
                            brelse(to);
                        }
                    }
                    ```
                    - 透過遍歷 log.lh（這在 `kernel/log.c` 中可以看到 struct log log 且 log struct 中有 lh）
                        - 搭配 `struct buf *from = bread(log.dev, log.lh.block[tail]);` 找出被修改過的 buffer
                        - 搭配 `struct buf *to = bread(log.dev, log.start+tail+1);` 找出磁碟上日誌區對應的 buffer
                    - 透過 `memmove`，把修改過的數據複製到日誌區對應的 buffer 中
                    - `bwrite` 把日誌區對應的 buffer 內容寫回到 disk 的日誌區上
                    - `brelse` 釋放這兩個緩衝區
                    
                    **這邊是寫入到 log area 中的 logged blocks**
                - write_head()：把 transaction 的 metadata 寫到 disk 上的 log header block，這個操作是 transaction 的 `commit point`（代表這個 transaction 已完成，就算系統崩潰，前面提交的修改還是會生效）
                    ```c
                    static void
                    write_head(void)
                    {
                        struct buf *buf = bread(log.dev, log.start);
                        struct logheader *hb = (struct logheader *) (buf->data);
                        int i;
                        hb->n = log.lh.n;
                        for (i = 0; i < log.lh.n; i++) {
                            hb->block[i] = log.lh.block[i];
                        }
                        bwrite(buf);
                        brelse(buf);
                    }
                    ```
                    - 透過 `bread` 把 disk 上的 log.start 移到 memory 的緩衝區中（`buf`）
                    - 透過 for loop 把追蹤的變更列表複製到 `buf` 中
                    - `bwrite` 把紀錄變更列表的 buffer 內容寫回到 disk 的日誌頭
                    - `brelse` 釋放緩衝區
                - install_trans()：將已經 `commit` 的變更從 disk 上的 `logged block` 寫到最終位置，會在兩種情況下被呼叫：
                    ```c
                    static void
                    install_trans(int recovering)
                    {
                        int tail;

                        for (tail = 0; tail < log.lh.n; tail++) {
                            struct buf *lbuf = bread(log.dev, log.start+tail+1); // read log block
                            struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst
                            memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst
                            bwrite(dbuf);  // write dst to disk
                            if(recovering == 0)
                                bunpin(dbuf);
                            brelse(lbuf);
                            brelse(dbuf);
                        }
                    }
                    ```
                    - 正常提交：此時 `recovering` 參數為 0
                    - 崩潰後恢復：此時 `recovering` 參數為 1
                    - 不論 `recovering` 參數，都需要做以下事情
                        - 透過 `struct buf *lbuf = bread(log.dev, log.start+tail+1); // read log block` 讀取日誌區中已經提交的區塊變更內容
                        - 透過 `struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst` 讀取實際要邊羹區塊在檔案系統的位置
                        - 透過 `memmove` 將變更後的資料複製過去
                        - 透過 `bwrite` 將含有最新變更的目標區塊寫回 disk
                        - 檢查 `recovering` 參數：
                            - 如果是 0 那代表是正常的 commit ，那需要幫 buffer 做 `bunpin`，因為在 `log_write` 中被 pin 起來了
                            - 如果是 1 那代表是崩潰後的復原，系統處於啟動階段，正在執行 `recover_from_log()` 進行崩潰恢復，所有先前被釘住的緩衝區（Buffer Cache 內的所有內容）都會被視為丟棄，系統會重新初始化核心狀態和緩衝區快取。
                        - `brelse` 釋放這兩個緩衝區
                - 把 `log.lh.n` 變成 0，如果運行到這邊，代表
                    - 變更都已經透過 `write_log` 紀錄
                    - transaction 也透過 `write_head` 提交
                    - 變更數據也透過 `install_trans` 寫到最終位置了
                - write_head()
                    - 在這邊最重要的事情是會把 `hb -> n` （disk 上的 log 空間）設為 0，來清空 disk 上的日誌頭並且標記為空閒
            - `recover_from_log` 是在系統啟動時執行，這樣可以確保如果上一次系統崩潰時還沒完成的 transaction 可以完成
                ```c
                static void
                recover_from_log(void)
                {
                    read_head();
                    install_trans(1); // if committed, copy from log to disk
                    log.lh.n = 0;
                    write_head(); // clear the log
                }
                ```
                過程會
                - 透過 `read_head` 讀取日誌頭，把內容放到 memory 的 log.lh 中
                - 透過 `install_trans(1)`， 如果 `log.lh.n` 為 0，那就不用做了
                - 設定 `log.lh.n = 0`
                - `write_head` 來清空 disk 上的日誌頭並且標記為空閒
6. Explain the roles of the in-memory inode functions `iget()` and `iput()`. Describe the purpose of the reference count (`ref`) in `struct inode`.
    - `iget` 實作在 `kernel/fs.c`
        ```c
        static struct inode*
        iget(uint dev, uint inum)
        {
            struct inode *ip, *empty;

            acquire(&itable.lock); // 鎖定 inode table

            // Is the inode already in the table?
            // 看現有的 inode
            empty = 0;
            for(ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++){
                // ip -> ref 代表現在正在被使用
                if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
                    ip->ref++;
                    // 釋放並回傳
                    release(&itable.lock);
                    return ip;
                }
                // 紀錄遇到的第一個空閒
                // empty == 0 因為 empty 一開始就被初始化為 0 了，所以 == 0 代表還沒分配到 inode
                // 在 iget 遍歷 inode table 過程中，如果遇到任何一個空閒，就會將 empty 設為該空閒位的 inode struct 地址
                if(empty == 0 && ip->ref == 0)    // Remember empty slot.
                    empty = ip;
            }

            // Recycle an inode entry.
            // 沒分配到
            if(empty == 0)
                panic("iget: no inodes");

            ip = empty;
            ip->dev = dev;
            ip->inum = inum;
            ip->ref = 1;
            ip->valid = 0;
            release(&itable.lock);

            return ip;
        }
        ```
        - 一開始先取得鎖
        - 接著去遍歷整個 inode table 看看有沒有需要的 inode
            - 如果找到那就增加 `ip -> ref`，並釋放鎖及回傳 ip（這邊要注意，是透過 `ip -> ref` 來判定 inode 是否有在被使用）
            - 在這個過程中，會去紀錄找到第一個空閒的 slot

                **為什麼找到空閒的 slot 之後不直接 break 呢？**

                **因為這比較像是備用的方案，首要目標是找到 inode，找不到才會使用空閒的 slot**
        - 如果遍歷後還是找不到，那就 `panic`
        - 接著就是使用找到的第一個空閒的 slot 來儲存新的 inode
            - 設置 `dev`
            - 設置 `inum`
            - 將 `ip -> ref` 改為 1
            - 將 `valid` 設為 0，這代表雖然在 mem 中已經有 inode 結構，但 disk 上的實際數據還沒被讀取，這樣做的好處是可以延遲 I/O 提升 `iget` 的效能
        - 釋放鎖
        - 回傳 ip
        
    - `iput` 實作在 `kernel/fs.c`，主要是減少 inode 的 memory 引用計數，並且可以在特定條件下執行 disk 資源的清理
        ```c
        void
        iput(struct inode *ip)
        {
            acquire(&itable.lock);
            // 不需要遍歷因為傳入的指標就可以指導正確位址
            if(ip->ref == 1 && ip->valid && ip->nlink == 0){
                // inode has no links and no other references: truncate and free.

                // ip->ref == 1 means no other process can have ip locked,
                // so this acquiresleep() won't block (or deadlock).
                acquiresleep(&ip->lock);

                release(&itable.lock);

                itrunc(ip);
                ip->type = 0;
                iupdate(ip);
                ip->valid = 0;

                releasesleep(&ip->lock);

                acquire(&itable.lock);
            }

            ip->ref--;
            release(&itable.lock);
        }
        ```
        - 取得鎖
        - 檢查條件
            - `ip -> ref == 1`，表示只有當前執行 `iput` 的 process 對這個 inode 的做引用
            - `ip -> valid`
                - 確保 `inode` 的內容已經從 disk 載入到 memory
                - memory 中的 `ip->type`, `ip->size`, `ip->addrs[]` 等欄位是有效的
            - `ip -> nlink == 0`，表示檔案系統中已經沒有任何 directory entry 指向這個 inode
        - 條件成立的話
            - 鎖定 inode
            - 釋放 itable 鎖
            - 透過 itrunc 釋放 inode 佔有的 block
            - 將 `ip -> type` 設為 0，代表空閒
            - 透過 `iupdate` 把變更寫入日誌，之後才會寫回 disk
            - 將 `valid` 改為 0
        - 不論條件是否成立
            - `ip -> ref` 減 1
            - 釋放 itable 的鎖
    - inode 中 ref 的目的是要紀錄有多少 process 或是其他結構在使用這個 inode，以確保 inode 在被使用期間不被回收，並在 ref 歸零時允許 itable slot 被重複使用，或在硬連結數（nlink）同時歸零時觸發磁碟資源的永久清理。


#### Implementation
我們依照 implementation 要修改的檔案一一說明，要修改的有：
- kernel/fs.h
- kernel/file.h
- kernel/fs.c 中的 bmap() 以及 itrunc()

首先是 `kernel/fs.h`

在這個檔案中總共修改三個地方
- NDIRECT 部分
    ```c
    #define NDIRECT 12
    ```
    變成
    ```c
    #define NDIRECT 7
    #define NFINDIRECT 5
    #define NDINDIRECT 1
    ```
    這是為了要達成 SPEC 中的目標，至少 66666 個 data blocks。

    NDIRECT = Number of Direct blocks

    NFINDIRECT = Number of First-level Indirect block pointers

    NDINDIRECT = Number of Doubly-Indirect block pointers

    透過這樣的組合，一個檔案的容量可以達到 66823 個 data blocks。
- MAXFILE 部分
    ```c
    #define MAXFILE (NDIRECT + NINDIRECT)
    ```
    變成
    ```c
    #define MAXFILE (NDIRECT + NFINDIRECT*NINDIRECT + NDINDIRECT*NINDIRECT*NINDIRECT)
    ```
    修改一個檔案最多可以有多少個 block 的定義
- dinode 中 addrs（這個 dinode 是 disk inode）
    ```c
    uint addrs[NDIRECT+1];   // Data block addresses
    ```
    變成
    ```c
    uint addrs[NDIRECT + NFINDIRECT + NDINDIRECT];   // Data block addresses
    ```
    因為已經改變了 data blocks 的結構，所以這邊也要修改，雖然總數不變，但用途不同

接著是 `kernel/file.h`
- inode 中 addrs（這個 inode 是 memory 上的 inode 副本，所以結構需要和 disk 上的一致）
    ```c
    uint addrs[NDIRECT+1];
    ```
    變成
    ```c
    uint addrs[NDIRECT + NFINDIRECT + NDINDIRECT];
    ```

再來是 `kernel/fs.c` 中的 `bmap()`

`bmap` 的任務是幫 inode 的 bn(logic) 找到對應的 physical block number，我們一樣可以把這個任務分為三階段來看
- NDIRECT：如果 bn 小於 `NDIRECT` 那代表是直接區塊，跟原本的做法沒什麼不同，直接從 ip->addrs[bn] 讀取或分配
- NFINDIRECT：如果 bn 不小於 `NDIRECT` 那代表不是直接區塊（有可能是一級區塊或是二級區塊），為了要做這個判斷，首先要把 bn -= NDIRECT，接下來會經過幾個步驟
    - 透過 if 判斷 bn 是否小於 NFINDIRECT * NINDIRECT，如果是則代表在一級區塊
        ```c
        if(bn < NFINDIRECT * NINDIRECT)
        ```
    - 計算是第幾個 singly indirect block，透過 `bn / NINDIRECT` 可以知道這是第幾個 SIB
        ```c
        uint sib_index = NDIRECT + (bn / NINDIRECT);
        ```
    - 計算在 single idnex block 中的偏移量
        ```c
        uint sib_offset = bn % NINDIRECT;
        ```
    - 檢查 singly indirect block 本身是否已經被分配了 physical block，若無的話透過 `balloc` 分配，`balloc` 回傳是 block number，所以之後要透過 `bread` 將這個 block number 的資料讀出來
        ```c
        if((addr = ip->addrs[sib_index]) == 0){
            addr = balloc(ip->dev);
            if(addr == 0)
                return 0;
            ip->addrs[sib_index] = addr;
        }
        ```
    - 確認有分配或是分配完成後，透過 `bread` 將 physical block 放到 memory buffer 中，並且透過 sib_a 指標指向整個 singly indirect block 的開頭（實際是包含 256 個 data block）
        ```c
        struct buf *sib_bp = bread(ip->dev, addr);
        uint *sib_a = (uint*)sib_bp->data;
        ```
    - 將 singly indirect block 讀出來後，再透過 `sib_offset` 檢查是否已經有分配 physical block 了
        - 沒有的話  
            - 透過 `balloc` 分配，並且修改 `sib_bp` 的 data 內容
            - 注意因為這邊修改了透過 `bread` 讀進來的 on-disk data structure，需要呼叫 `log_write` 來寫回磁碟
        - 有的話就不需要做分配
            ```c
            if((addr = sib_a[sib_offset]) == 0){
                addr = balloc(ip->dev);
                if(addr){
                    sib_a[sib_offset] = addr;
                    // 因為修改 disk 上的資料結構，所以要寫到 log 中
                    log_write(sib_bp);
                }
            }
            ```
    - 透過 `brelse` 釋放 `sib_bp` 這個 buffer
        ```c
        brelse(sib_bp);
        ```
    - return logic block number 對應的 physical block number
        ```c
        return addr;
        ```
- NDINDIRECT：如果 bn 不是直接區塊也不是一級區塊，那 bn 會經過兩次調整。首先是 bn -= NDIRECT，再來是 bn -= NFINDIRECT * NINDIRECT（排除一級區塊），接下來會經過幾個步驟
    - 判斷調整後的 bn 是否在二級區塊的範圍內 (< 65536)
        ```c
        if(bn < NINDIRECT * NINDIRECT)
        ```
    - 透過 dib_index 表示 DIB（doubly indirect block）在 ip -> addrs 中的 index，只有一個所以可以這樣算
        ```c
        uint dib_index = NDIRECT + NFINDIRECT;
        ```
    - 如果 ip->addrs[dib_index] 還沒分配 physical block 就要透過 `balloc` 分配
        ```c
        if((addr = ip->addrs[dib_index]) == 0){
            addr = balloc(ip->dev);
            if(addr == 0)
                return 0;
            ip->addrs[dib_index] = addr;
        }
        ```
    - 確認有分配或是分配完成後，透過 `bread` 將 physical block 放到 memory buffer 中，並且透過 dib_a 指標指向整個 doublely indirect block 的開頭（實際是包含 256 個 SIB 指標的陣列）
        ```c
        struct buf *dib_bp = bread(ip->dev, addr);
        uint *dib_a = (uint*)dib_bp->data;
        ```
    - 計算是第幾個 singly indirect block，透過 `bn / NINDIRECT` 可以知道這是第幾個 SIB
        ```c
        uint sib_index_in_dib = bn / NINDIRECT;
        ```
    - 計算在 single idnex block 中的偏移量
        ```c
        uint data_offset_in_sib = bn % NINDIRECT;
        ```
    - 如果 singly indirect block（sib_index_in_dib） 尚未被分配到 physical block，則透過 `balloc` 分配
        ```c
        uint sib_addr;
        if((sib_addr = dib_a[sib_index_in_dib]) == 0){
            sib_addr = balloc(ip->dev);
            if(sib_addr == 0) {
                brelse(dib_bp);
                return 0; 
            }
            dib_a[sib_index_in_dib] = sib_addr;
            log_write(dib_bp);
        }
        ```
        - 注意因為這邊修改了透過 `bread` 讀進來的 on-disk data structure，需要呼叫 `log_write` 來寫回磁碟
    - 結束後釋放 buffer cache
        ```c
        brelse(dib_bp);
        ```
    - 讀取 `sib_addr`（可能是原本就分配好或是剛剛透過 `addr` 分配的），並且透過 sib_a 指標指向整個 singly indirect block 的開頭（實際是包含 256 個 data block 號碼的陣列）
        ```c
        struct buf *sib_bp = bread(ip->dev, sib_addr);
        uint *sib_a = (uint*)sib_bp->data;
        ```
     - 將 singly indirect block 讀出來後，再透過 `data_offset_in_sib` 檢查是否已經有分配 physical block 了
        - 沒有的話  
            - 透過 `balloc` 分配，並且修改 `sib_bp` 的 data 內容
            - 注意因為這邊修改了透過 `bread` 讀進來的 on-disk data structure，需要呼叫 `log_write` 來寫回磁碟
        - 有的話就不需要做分配
        ```c
        if((addr = sib_a[data_offset_in_sib]) == 0){
            addr = balloc(ip->dev);
            if(addr){
                sib_a[data_offset_in_sib] = addr;
                // 因為修改 disk 上的資料結構，所以要寫到 log 中
                log_write(sib_bp);
            }
        }
        ```
    - 透過 `brelse` 釋放 `sib_bp` 這個 buffer
        ```c
        brelse(sib_bp);
        ```
    - return logic block number 對應的 physical block number
        ```c
        return addr;
        ```
- 若是三個區塊都不符合，則出現 panic
    ```c
    panic("bmap: out of range");
    ```



最後是 `kernel/fs.c` 中的 itrunc()

`itrunc` 的任務是釋放 inode 的所有 data blocks 並清空檔案內容，我們一樣可以把這個任務分為三階段來看
- NDIRECT：釋放所有直接區塊，包括呼叫 `bfree` 釋放 block 以及將 ip -> addrs[i] 設為 0
    ```c
    for(i = 0; i < NDIRECT; i++){
        if(ip->addrs[i]){
        bfree(ip->dev, ip->addrs[i]);
        ip->addrs[i] = 0;
        }
    }
    ```
- NFINDIRECT：這是 singly indirect block，要做幾個步驟
    - 把 singly indirect block 透過 `bread` 讀入 buffer 中（`bp`），必且透過 a 指向 bp -> data（就是 256 個 data block 的指標）
        ```c
        for(i = NDIRECT; i < NDIRECT + NFINDIRECT; i++){
        if(ip -> addrs[i]){
        // 讀取 SIB
        bp = bread(ip->dev, ip->addrs[i]);
        a = (uint*)bp->data;
        ...
        ```
    - 對 bp -> data 中的 256 個 data block 做遍歷，查看是否有在使用，若是有則呼叫 `bfree` 做釋放
        ```c
        for(j = 0; j < NINDIRECT; j++){
            if(a[j]){
            // bfree 裡面就會呼叫 log_write
            bfree(ip->dev, a[j]);
            }
        }
        ```
    - 呼叫 `brelse` 釋放 buffer
        ```c
        brelse(bp)
        ```
    - 釋放 singly indirect block 本身
        ```c
        // 釋放 SIB 本身
        bfree(ip -> dev, ip->addrs[i]);
        ip -> addrs[i] = 0;
        ```
    **以上動作因為可能有多個 singly indirect block 所以要透過 for loop 重複做**
- NDINDIRECT：這是 doubly indirect block，要做幾個步驟
    - 先判斷有沒有在使用，有的話就接著做
        ```c
        uint dib_index = NDIRECT + NFINDIRECT;
        if(ip -> addrs[dib_index])
        ```
        - 把 doubly indirect block 透過 `bread` 讀入 buffer 中（dib_bp），並且透過 dib_a 指向 dib_bp -> data（就是 256 個 singly indirect block 的指標）
            ```c
            struct buf *dib_bp = bread(ip->dev, ip->addrs[dib_index]);
            uint *dib_a = (uint*)dib_bp->data;
            ```
        - 針對每一個指向 singly indirect block 的指標

            如果有在使用，就再透過 `bread` 將 singly indirect block 讀進 memory buffer（sib_bp）。再透過 sib_a 指向 sib_bp -> data（就是 256 個 data block 的指標）
            ```c
            for(k = 0; k < NINDIRECT; k++){
            uint sib_addr = dib_a[k];
            if(sib_addr){
                // 讀取 SIB
                struct buf *sib_bp = bread(ip->dev, sib_addr);
                uint *sib_a = (uint*)sib_bp->data;
            ...
            ```
            - 接著透過 for 迴圈確認 data block 是否有使用，若是有則使用 `bfree` 將其釋放（這不需要讀到 memory buffer，因為只要知道 physical block number 即可）
                ```c
                    for(j = 0; j < NINDIRECT; j++){
                        if(sib_a[j]){
                            bfree(ip->dev, sib_a[j]);
                        }
                    }
                ```
            - 透過 `brelse` 釋放 buffer（這是 singly indirect block）
                ```c
                brelse(sib_bp);
                ```
            - 透過 `bfree` 釋放 singly indirect block 本身
                ```c
                bfree(ip->dev, sib_addr);
                ```
        - 透過 `brelse` 釋放 buffer（這是 doubly indirect block）
            ```c
            brelse(dib_bp);
            ```
        - 透過 `bfree` 釋放 doubly indirect block 本身
            ```c
            bfree(ip->dev, ip->addrs[dib_index]);
            ip->addrs[dib_index] = 0;
            ```
- 更新 inode 大小，並將修改後的 indoe 寫回 disk
    ```c
    ip ->size = 0;
    iupdate(ip);
    ```

# Part 2
#### Implementation
這部分只需要修改 `kernel/sysfile.c` 中的兩個地方
- sys_symlink 函式
    - sys_symlink 的主要目的是建立一個符號連結檔案，並把目標路徑字串寫進去，需要幾個步驟：
        - 取得 target（要透過連結指向的檔案） 跟 path（建立連結的位置）
            ```c
            if(argstr(0, target, MAXPATH) <0 || argstr(1, path, MAXPATH) < 0)
                return -1;
            ```
        - 透過 begin_op() 開始操作
            ```c
            begin_op();
            ```
        - 透過 create 建立一個新的 inode， type 設定為 T_SYMLINK ，並且把 major & minor 都設為 0
            ```c
            ip = create(path, T_SYMLINK, 0, 0); // 這在 kernel/stat.h 中
            if(ip == 0){
                end_op();
                return -1;
            }
            ```
            
            `create` 在 `kernel/sysfile.c` 中，會做以下步驟：
            - 尋找 path 的父目錄
            - 鎖定父目錄
            - 檢查目標名稱是否已存在，且類型符合要求
                - 需要建立的 type 是 `T_FILE` 或是 `T_DEVICE` 就直接回傳 inode
                - 否則回傳 0
            - 如果要的目標名稱不存在，那就建立一個新的 inode
            - 初始化 inode 的屬性
            - 將 inode 更新到磁碟
            - 如果建立的是目錄，需要在內部建立跟自己（.）以及父目錄（..）的連結（透過 inode number）
                - **這邊要注意的是自己跟自己連結，不需要增加 nlink 計數**
            - 也要將新建立的 inode 加到父目錄中
                - 這邊的 `nlink` 增加，已經在初始化 inode 屬性就做了
            - 增加父目錄的 `nlink` 計數
            - 解鎖並釋放父目錄
            - 另外有一個區塊是 fail，會做：
                - 將 inode 的 `nlink` 改為 0
                - 寫到 disk
                - 解鎖然後釋放 inode
                - 解鎖然後釋放父目錄
                - 回傳失敗
            - 以下情況會進到 fail
                - 建立目錄的 `.` 或 `..` 失敗
                - 無法將建立的 node 加到父目錄
        - 將 target 透過 writei 寫入剛建立的 inode 中
            ```c
            if(writei(ip, 0, (uint64)target, 0, strlen(target)) != strlen(target)){
                // 如果寫入失敗，釋放 inode 並結束操作
                iunlockput(ip);
                end_op();
                return -1;
            }
            ```
        - 完成後釋放 inode
            ```c
            iunlockput(ip);
            end_op();
            ```
- sys_open 函式
    - sys_open 的主要目開啟一個檔案，並回傳 file descriptor 給使用者程式，需要幾個步驟：
        - 透過 argint 讀取 omode
            ```c
            argint(1, &omode);
            ```
        - 透過 argstr 取得第 0 個參數存到 path 中
            ```c
            if((n = argstr(0, path, MAXPATH)) < 0)
                return -1;
            ```
        - 透過 begin_op() 開始操作
        - 經過一系列檢查，主要有三個檢查
            - 透過 `O_CREATE` flag 確認是否是要建立新檔案
                - 是的話，透過 `CREATE` 建立，失敗的話 `end_op`，回傳 -1
                - 不是的話，透過 `namei` 去找 inode
                    - 失敗的話 `end_op`，回傳 -1
                    - 成功的話 lock 找到的 inode，檢查如果是目錄，那 `omode` 只能是 `O_RDONLY`（如果沒有這樣，那使用者可以透過 open + write 去修改 dirtectory structure，會很危險）
            ```c
            if(omode & O_CREATE){
                ip = create(path, T_FILE, 0, 0);
                if(ip == 0){
                    end_op();
                    return -1;
                }
            } else {
                if((ip = namei(path)) == 0){
                    end_op();
                    return -1;
                }
                ilock(ip);
                if(ip->type == T_DIR && omode != O_RDONLY){
                    iunlockput(ip);
                    end_op();
                    return -1;
                }
            }
            ```
        - 檢查設備檔案，防止開啟無效的設備檔案
            ```c
            if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
                iunlockput(ip);
                end_op();
                return -1;
            }
            ```
        - 透過 `filealloc` 從系統的 file table 中分配一個 struct file 及透過 `fdalloc(f)` 從 process 的 file descriptor table 分配一個 fd 編號。如果任一失敗那就要清理資源並回傳 -1
            ```c
            if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
                if(f)
                    fileclose(f);
                iunlockput(ip);
                end_op();
                return -1;
            }
            ```
        ##### 接下來是 part 2 新增的部分
        - 如果 ip -> type == T_SYMLINK，就要接著做判斷
            - 如果 omode 是 O_NOFOLLOW 代表不要追蹤符號連結，那就要：
                - 釋放 inode
                - 結束操作並回傳 -2
                    ```c
                    if(omode & O_NOFOLLOW){
                        // 如果有這個 flag，那代表不追蹤符號連結，回傳 -2
                        iunlockput(ip);
                        end_op();
                        return -2;
                    }
                    ```
            - 否則的話，進入 for loop
                - 透過 `readi` 把符號連結的內容（一個路徑字串）讀到 target
                - 釋放 inode，這是目前的符號連結 inode（前面有把 ip 鎖起來）
                - 透過 `namei` 將 target 對應的 inode 找出來（將其變成新一輪的 inode）
                - 把 inode 再鎖起來
                - 如果 ip -> type 不是 T_SYMLINK 就跳出 for loop，或是 depth >= 5 也會跳出
            - 檢查 depth 
                - 如果大於等於 5 就結束操作並回傳 -3
                    ```c
                    for(depth = 0; depth < 5; depth++){
                        // 讀取 symbol 指向的路徑
                        if(readi(ip, 0, (uint64)target, 0, MAXPATH) <= 0){
                            iunlockput(ip);
                            end_op();
                            return -1;
                        }

                        // 釋放目前的符號連結 inode
                        iunlockput(ip);

                        // 開啟 target 指向的檔案
                        if((ip = namei(target)) == 0){
                            end_op();
                            return -1;
                        }

                        // 鎖著目標 inode
                        ilock(ip);

                        // 如果不是符號連結，跳出迴圈
                        if(ip -> type != T_SYMLINK){
                            break;
                        }
                    }

                    if(depth >= 5){
                        iunlockput(ip);
                        end_op();
                        return -3;
                    }
                    ```
                - 檢查沒問題的話就要去設定 file descriptor 的屬性
                    - 首先設定檔案類型並且依據不同的類型做其他初始化
                        - 設備：紀錄設備編號
                        - 普通檔案：設定 offset = 0
                            ```c
                            if(ip->type == T_DEVICE){
                                f->type = FD_DEVICE;
                                f->major = ip->major;
                            } else {
                                f->type = FD_INODE;
                                f->off = 0;
                            }
                            ```
                    - 設定指向 inode
                        ```c
                        f -> ip = ip; // 指向 node
                        ```
                    - 設定是否可讀可寫
                        ```c
                        f->readable = !(omode & O_WRONLY); // 不是 WRONLY 就可讀
                        f->writable = (omode & O_WRONLY) || (omode & O_RDWR); // WRONLY 或 RDWR 可寫
                        ```
                    - 如果有設定 O_TRUNC，那在開啟檔案時會把檔案內容清空
                        ```c
                        if((omode & O_TRUNC) && ip->type == T_FILE){
                            itrunc(ip);  // 把檔案內容全部刪除，size = 0
                        }
                        ```
                        
                        itrunc 會釋放 inode 所有的 data blocks，並且更新檔案大小為 0
                        
                    - 釋放 inode 結束操作並回傳 fd
                        ```c
                        iunlock(ip);   // 解鎖 inode
                        end_op();      // 結束 transaction
                        return fd;     // 回傳 file descriptor 給使用者
                        ```
# Part 3
#### Implementation
這部分我們需要先釐清 Part2 和 Part3 需求的不同：
- Part2 是需要解析 path 路徑中最終 file 的符號連結
- Part3 是需要解析在 path 中間所遇到的符號連結

因此這邊需要做好分工：
- namex(`kernel/fs.c`)：只解析「path 中間」的符號連結
    - 如果遇到符號連結且後面還有路徑，就要進行符號的跳轉。
    - 但如果這是路徑的最後一個元素，就要回傳該 inode 給 sys_open 或 sys_chdir
- sys_chdir(`kernel/sysfile.c`)：解析最終目標

所以這邊需要修改 `sys_chdir` 以及 `namex` 兩個函數
- `sys_chdir`

    原本的程式
    ```c
    uint64
    sys_chdir(void)
    {
        // TODO: Symbolic Link to Directories
        // You can modify this to cd into a symbolic link
        // The modification may not be necessary,
        // depending on you implementation.
        char path[MAXPATH];
        struct inode *ip;
        struct proc *p = myproc();

        begin_op();
        // namei 回傳路徑中最後一個 element 的 inode
        if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
            end_op();
            return -1;
        }
        ilock(ip);
        // 如果發現最後要 cd 的 inode 根本不是 directory
        if(ip->type != T_DIR){
            iunlockput(ip);
            end_op();
            return -1;
        }
        iunlock(ip);
        iput(p->cwd);
        end_op();
        // 修改 cwd
        p->cwd = ip;
        return 0;
    }
    ```

    需要新增如果透過 `namei` 呼叫 `namex` 後回傳的 inode type 是 `T_SYMLINK` 的處理
    ```c
    uint64
    sys_chdir(void)
    {
        // TODO: Symbolic Link to Directories
        // You can modify this to cd into a symbolic link
        // The modification may not be necessary,
        // depending on you implementation.
        char path[MAXPATH];
        struct inode *ip;
        struct proc *p = myproc();
        
        begin_op();
        if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
            // 透過 namei 會找到符號連結指向的目標並且返回為 ip
            end_op();
            return -1;
        }
        
        ilock(ip);
        
        + // 如果是 symlink，追蹤它
        + if(ip->type == T_SYMLINK){
        +     char target[MAXPATH];
        +     int depth;
            
        +     for(depth = 0; depth < 5; depth++){
        +       if(readi(ip, 0, (uint64)target, 0, MAXPATH) <= 0){
        +           iunlockput(ip);
        +           end_op();
        +           return -1;
        +       }
            
        +       iunlockput(ip);
            
        +       if((ip = namei(target)) == 0){
        +           end_op();
        +           return -1;
        +       }
            
        +       ilock(ip);
            
        +       if(ip->type != T_SYMLINK){
        +           break;
        +       }
        +     }
            
        +     if(depth >= 5){
        +     iunlockput(ip);
        +     end_op();
        +     return -1;
        +     }
        + }
        
        if(ip->type != T_DIR){
            iunlockput(ip);
            end_op();
            return -1;
        }
        
        iunlock(ip);
        iput(p->cwd);
        end_op();
        p->cwd = ip;
        return 0;
    }
    ```

    新增的部分跟 sys_open 中在做 symlink 的追蹤是相同的，會不斷追蹤直到超過五層或是 type 不是 `T_SYMLINK`

- `namex`
    
    原本的程式
    ```c
    static struct inode*
    namex(char *path, int nameiparent, char *name)
    {
        // TODO: Symbolic Link to Directories
        // Modify this function to deal with symbolic links to directories.
        struct inode *ip, *next;

        if(*path == '/')
            // 從根目錄開始解析路徑
            // iget 的目的是在 inode table 中找到對應的 inode並將他的 ref +1
            // root directory 不確定是否已經在 inode table 中
            ip = iget(ROOTDEV, ROOTINO);
        else
            // 從當前工作目錄開始解析路徑
            // 會用 idup 是因為目前已經持有自己的 cwd
            // idup 的作用是增加一個 「已經存在且已經被打開」的 inode 的 ref 計數
            ip = idup(myproc()->cwd);

        while((path = skipelem(path, name)) != 0){
            // skipelem 每次會把一個路徑字串的第一個 element 取出來放到 name 中
            // 回傳新的 path 字串（去掉剛剛取出的 element）
            ilock(ip);
            // 如果不是目錄就錯誤
            // 在整個解析的過程，只有最後一個元素可以不是目錄
            if(ip->type != T_DIR){
                iunlockput(ip);
                return 0;
            }
            // 因為 nameiparent 的話，只需要找到 parent 就好
            // 但 path == '\0' 代表已經沒有路徑了
            // 例如 a/b/c ，當解析到 c 的時候，path 就會是 '\0'

            if(nameiparent && *path == '\0'){
                iunlock(ip);
                return ip;
            }
                // 在當前的目錄中尋找剛取出來的 element
            if((next = dirlookup(ip, name, 0)) == 0){
                iunlockput(ip);
                return 0;
            }
            iunlockput(ip);
            ip = next;
        }
        if(nameiparent){
            iput(ip);
            return 0;
        }
        return ip;
    }
    ```

    原本程式只是要去將 path 解析成對應的 inode，所以這邊主要是新增路徑中間元素遇到符號連結的處理以及嵌套的符號連結
    
    （例如: a/b/c，其中 a -> d -> e -> f），針對這種情況採用的是單個節點做 traverse，如果沒有超過五層那就繼續往下並且重置深度

    修改過後的程式
    ```c
    static struct inode*
    namex(char *path, int nameiparent, char *name)
    {
        struct inode *ip, *next;

         // 從根目錄開始解析路徑
        // iget 的目的是在 inode table 中找到對應的 inode並將他的 ref +1
        // root directory 不確定是否已經在 inode table 中
        if(*path == '/')
            ip = iget(ROOTDEV, ROOTINO);
        else
        // 從當前工作目錄開始解析路徑
        // 會用 idup 是因為目前已經持有自己的 cwd
        // idup 的作用是增加一個 「已經存在且已經被打開」的 inode 的 ref 計數
            ip = idup(myproc()->cwd);

        while((path = skipelem(path, name)) != 0){
            // skipelem 每次會把一個路徑字串的第一個 element 取出來放到 name 中
            // 回傳新的 path 字串（去掉剛剛取出的 element）
            ilock(ip);
            // 如果不是目錄就錯誤
            // 在整個解析的過程，只有最後一個元素可以不是目錄
            if(ip->type != T_DIR){
                iunlockput(ip);
                return 0;
            }

            // 因為 nameiparent 的話，只需要找到 parent 就好
            // 但 path == '\0' 代表已經沒有路徑了
            // 例如 a/b/c ，當解析到 c 的時候，path 就會是 '\0'
            if(nameiparent && *path == '\0'){
                iunlock(ip);
                return ip;
            }

            // 在當前目錄尋找剛取出來的 element
            if((next = dirlookup(ip, name, 0)) == 0){
                iunlockput(ip);
                return 0;
            }
            iunlockput(ip);
            // 修改 ip
            ip = next;

            // 代表現在在解析的路徑不是最後一個 element
            if(*path != '\0'){ 
            // 每個 element 的深度獨立計算
            int depth = 0; 

            while(1){
                ilock(ip);
                if(ip->type != T_SYMLINK){
                    iunlock(ip);
                    // 如果不是連結，代表已解開到真實目錄
                    break; 
                }

                // 偵測單次 traverse 深度是否超過限制
                if(++depth >= 5){
                    iunlockput(ip);
                    return 0; 
                }

                char target[MAXPATH];
                // 讀取連結目標路徑字串
                if(readi(ip, 0, (uint64)target, 0, MAXPATH) <= 0){
                    iunlockput(ip);
                    return 0;
                }
                iunlockput(ip);

                struct inode *tip;
                // 這邊只要考慮絕對路徑
                if(target[0] == '/')
                    tip = iget(ROOTDEV, ROOTINO);
                else {
                    tip = iget(ROOTDEV, ROOTINO);
                }

                char subname[DIRSIZ];
                char *tptr = target;
                // 將 target 字串 (如 "d/e/f") 解析成最後一個 inode
                while((tptr = skipelem(tptr, subname)) != 0){
                    ilock(tip);
                    struct inode *tn = dirlookup(tip, subname, 0);
                    iunlockput(tip);
                    if((tip = tn) == 0) return 0; // 目標路徑不存在
                }

                // 更新目前的 ip 為解析後的目標，繼續內層 while(1) 
                // 這樣可以處理「連結指向連結」的嵌套情況
                ip = tip;
            }   
          }
        }

        // 處理 nameiparent 失敗的情況
        if(nameiparent){
            iput(ip);
            return 0;
        }

        return ip;
    }
    ```



    