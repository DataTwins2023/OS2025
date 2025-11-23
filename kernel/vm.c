#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[]; // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S



// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}

// Initialize the one kernel_pagetable
void kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if (va >= MAXVA)
    panic("walk");

  for (int level = 2; level > 0; level--)
  {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V)
    {
      pagetable = (pagetable_t)PTE2PA(*pte);
    }
    else
    {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if (size == 0)
    panic("mappages: size");

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for (;;)
  {
    if ((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if (*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
// mp3 TODO
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE; a += PGSIZE)
  {
    if ((pte = walk(pagetable, a, 0)) == 0) // page table 不存在
      // panic("uvmunmap: walk");
      continue;
    if ((*pte & PTE_V) == 0) // page table entry not mapped
      // panic("uvmunmap: not mapped");
      continue;
    if (PTE_FLAGS(*pte) == PTE_V) // not a leaf (所謂not a leaf 指的是沒有 R/W/X 權限只有 V)
      panic("uvmunmap: not a leaf");

    if (do_free)
    {
      uint64 pa = PTE2PA(*pte);
      kfree((void *)pa);
    }
    *pte = 0;
  }
}

// create an allocate a page for putting an empty L2 user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0) // run out of the memory kalloc return 0
    return 0;
  memset(pagetable, 0, PGSIZE); // clear the allocated page
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if (sz >= PGSIZE)
    panic("uvmfirst: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
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

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if (newsz >= oldsz)
    return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
  {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    }
    else if (pte & PTE_V)
    {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz; i += PGSIZE)
  {
    if ((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if ((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0)
    {
      kfree(mem);
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0)
  {
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0)
  {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0)
  {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max)
      n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0)
    {
      if (*p == '\0')
      {
        *dst = '\0';
        got_null = 1;
        break;
      }
      else
      {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null)
  {
    return 0;
  }
  else
  {
    return -1;
  }
}


/* Map pages to physical memory or swap space. */
int madvise(uint64 base, uint64 len, int advice)
{
  struct proc *p = myproc();
  
  // 1. 檢查範圍是否合法
  // base 和 len 必須大於等於 0 (unsigned 已經保證)，且範圍不能超過 process 大小
  if (base > p->sz || (base + len) > p->sz) {
    return -1;
  }
  
  if (len == 0) return 0;

  // 2. Handle normal advice: Do nothing
  if (advice == MADV_NORMAL) {
    return 0;
  }

  // 3. Handle Swap Out 
  if (advice == MADV_DONTNEED) {
    pte_t *pte;
    uint64 start = PGROUNDDOWN(base);
    uint64 end = PGROUNDUP(base + len);

    for (uint64 va = start; va < end; va += PGSIZE) {
      // 取得 PTE，不需要分配新的 Page Table (最後參數為 0)
      pte = walk(p->pagetable, va, 0);
      
      // 如果 PTE 不存在，或是無效 (可能已經 swapped 或根本沒分配)，則跳過
      if (pte == 0 || !(*pte & PTE_V))
        continue;

      //Transactional disk operation 
      begin_op();
      
      // 1. allocate a block on disk
      uint blockno = balloc_page(ROOTDEV);
      
      // 2. write memory content to disk
      uint64 pa = PTE2PA(*pte);
      write_page_to_disk(ROOTDEV, (char*)pa, blockno);
      
      // 3. 修改 PTE
      // 清除 Valid bit, 設定 Swapped bit
      // 將 Block Number 存入 PTE (原本放 PA 的位置)
      // 保留原本的權限 Flags (R/W/X/U)
      *pte = BLOCKNO2PTE(blockno) | PTE_FLAGS(*pte) | PTE_S;
      *pte &= ~PTE_V;
      
      // End the transactional disk operation
      end_op();
      
      // 4. free the physical memory
      kfree((void*)pa);
    }
    // Flush the TLB 
    sfence_vma();
    return 0;
  }

  // 4. 處理 MADV_WILLNEED (Swap In)
  if (advice == MADV_WILLNEED) {
     pte_t *pte;
    uint64 start = PGROUNDDOWN(base);
    uint64 end = PGROUNDUP(base + len);

    for (uint64 va = start; va < end; va += PGSIZE) {
      // 取得 PTE，如果 Page Table 頁面不存在則建立 (alloc=1)
      // 因為我們"Will Need"這塊記憶體，所以必須確保路徑存在
      pte = walk(p->pagetable, va, 1);
      if (pte == 0) return -1; // 系統記憶體不足

      // 情況 A: 頁面在磁碟中 (Swapped)
      if (*pte & PTE_S) {
        uint blockno = PTE2BLOCKNO(*pte);
        
        // 分配新的物理頁面
        char *pa = kalloc();
        if (pa == 0) return -1; // OOM
        
        begin_op();
        // 從磁碟讀回資料
        read_page_from_disk(ROOTDEV, pa, blockno);
        // 釋放磁碟區塊 (因為已經讀回 RAM 了)
        bfree_page(ROOTDEV, blockno);
        end_op();
        
        // 更新 PTE: 指向新的 PA，設定 Valid，清除 Swapped
        *pte = PA2PTE((uint64)pa) | PTE_FLAGS(*pte) | PTE_V;
        *pte &= ~PTE_S;
      } 
      // 情況 B: 頁面根本還沒分配 (Lazy Allocation)
      else if (!(*pte & PTE_V)) {
        char *pa = kalloc();
        if (pa == 0) return -1;
        memset(pa, 0, PGSIZE);
        
        // 設定新頁面的映射，給予完整權限
        *pte = PA2PTE((uint64)pa) | PTE_R | PTE_W | PTE_X | PTE_U | PTE_V;
      }
      // 情況 C: 頁面已經在記憶體中 (什麼都不做)
    }
    return 0;
  }

  return 0;
  panic("not implemented yet\n");
}




/* Print multi layer page table. */

/* vmprint_rec 
pagetable: page table to print
level:
indent:


*/
static void vmprint_rec(pagetable_t pagetable, int level, int indent, uint64 va_base);

void
vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);
  vmprint_rec(pagetable, 2, 2, 0); // level=2, indent=2 spaces, base_va=0
}

void
vmprint_rec(pagetable_t pagetable, int level, int indent, uint64 va_base)
{
  // iterate all PTEs: 每個 pte 大小為 8 bytes，一頁 4096 bytes，有 512 個 PTEs
  for(int i = 0; i < 512; i++){ 
    pte_t pte = pagetable[i];

    if(!(pte & PTE_V) && !(pte & PTE_S)) // invalid but swapped PTE should be printed as well  
      continue;

    uint64 pa = PTE2PA(pte);

    uint64 va = va_base | ((uint64)i << (level * 9 + 12));

    // 印出縮排
    for(int s = 0; s < indent; s++)
      printf(" ");

    //printf("%d: pte=%p va=%p pa=%p V", i, pte, va, pa); //印出 pa version　
    printf("%d: pte=%p va=%p ", i, pte, va); // 印出 pte version

    if (pte & PTE_S) {
      // 從 PTE 取出 block number
      uint64 blockno = PTE2BLOCKNO(pte);
      // 印出其 後半部 pte 資訊
      printf("pa=%p blockno=%p", (uint64)blockno << 12, blockno); 
    } else {
      // 如果是一般的 page 則直接印出其 pa
      printf("pa=%p", pa);
    }

    if(pte & PTE_V) printf(" V");
    if(pte & PTE_R) printf(" R");
    if(pte & PTE_W) printf(" W");
    if(pte & PTE_X) printf(" X");
    if(pte & PTE_U) printf(" U");
    if(pte & PTE_S) printf(" S");

    printf("\n");

    // 若是 valid page 且不是最底層(level > 0)，則繼續遞迴 L1/L0
    if((pte & PTE_V) && level > 0) {
      vmprint_rec((pagetable_t)pa, level - 1, indent + 2, va);
    }
  }
}