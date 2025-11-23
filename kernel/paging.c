#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "proc.h"

/* Page fault handler */
int handle_pgfault()
{
    struct proc *p = myproc();
    /* 1. Find the address that caused the fault */
    uint64 va = r_stval();
    /* uint64 va = r_stval(); */
    /* 2. align the address to the page boundary */ 
    va = PGROUNDDOWN(va);

    /* 3. allocate a frame (demand on pagging) */
    char *pa = kalloc();
    if (pa == 0) {
        return -1; // no memory space
    }
    memset(pa, 0, PGSIZE);

    /* 4. Mapping the page table entry */
    if (mappages(p->pagetable, va, PGSIZE, (uint64)pa, PTE_W | PTE_R | PTE_X | PTE_U) != 0) {
        kfree(pa); // mapping failed, free the allocated page
        return -1;
    }
    return 0;
    /* mp3 TODO */
    panic("not implemented yet\n");
}
