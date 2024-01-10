#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "mmap.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"

pte_t * walkpgdir(pde_t *pgdir, const void *va, int alloc);
int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);

struct mmap_region* find_mmap_region(void* addr) {

    struct proc *currProc = myproc();

    for(int i = 0; i < NELEM(currProc->mmaps); i++) {

        if(currProc->mmaps[i].size > 0) {

            uint start = currProc->mmaps[i].virt_addr;
            uint end = start + currProc->mmaps[i].size;

            // Check if the given address is within the range of this mapping.
            if ((uint)addr >= start && (uint)addr < end) {
                return &currProc->mmaps[i];
            }

        }

    }

    return (void*) -1;

}

int my_unmap(void* addr, int length) {
    struct proc *curproc = myproc();

    uint startAddr = (uint)PGROUNDDOWN((uint)addr);
    int numPages = (length + PGSIZE - 1) / PGSIZE;

    for (int i = 0; i < numPages; i++) {
        char *mem = (char *)startAddr + i * PGSIZE;

        pte_t *pte = walkpgdir(curproc->pgdir, mem, 0);
        if (!pte || !(*pte & PTE_P)) {
            continue; // or handle error
        }

        struct mmap_region *region = find_mmap_region(mem);
        if (region && (region->flags & MAP_SHARED) && region->f != 0) {
            struct file *f = region->f;
            int fileOffset = region->offset + ((uint)mem - region->virt_addr);
            f->off = fileOffset;
            filewrite(f, mem, PGSIZE);
        }

        // Free the physical page and clear PTE
        char *pa = P2V(PTE_ADDR(*pte));
        kfree(pa);
        *pte = 0;
    }


    return 0;
}
