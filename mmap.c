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

#define MMAP_BASE 0x60000000

int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);
pte_t * walkpgdir(pde_t *pgdir, const void *va, int alloc);

int is_mapped(pde_t *pgdir, void *va, int size) {

	char *a = (char *)PGROUNDDOWN((uint)va); // Align address down to page boundary
	char *last = (char *)PGROUNDDOWN(((uint)va) + size - 1); // Address of the last byte

	for(; a <= last; a += PGSIZE) {

		pde_t *pde = &pgdir[PDX(a)];

		if (!(*pde & PTE_P)) {

			// Page directory entry is not present.
			return 0;

		}

		pte_t *pte = (pte_t*)P2V(PTE_ADDR(*pde));
		pte_t *ptentry = &pte[PTX(a)];

		if (!(*ptentry & PTE_P)) {

			// Page table entry is not present.
			return 0;

		}
	}
	
	return 1;
}

void *my_mmap(void *addr, int size, int prot, int flags, int fd, int offset) {

	// cprintf("WE GOT HERE\n");

	struct proc *curproc = myproc(); // Get current process
	void* addrArg = addr;

	if(size <= 0 || offset < 0) {

		return (void *) -1;

	}

  	if((flags & MAP_FIXED) && (((int) addrArg < MMAP_BASE) || ((int) addrArg > KERNBASE) || ((uint)addrArg % PGSIZE) != 0 || is_mapped(curproc->pgdir, addrArg, size))) {

    	return (void *)-1;

  	}

	if (!(flags & MAP_ANONYMOUS) && (fd < 0 || fd >= NOFILE || curproc->ofile[fd] == 0)) {
		return (void *)-1;
	}

  	// Find a free mmap_region entry
  	struct mmap_region *mmap_entry = 0;
  	
	for(int i = 0; i < NELEM(curproc->mmaps); i++) {
    
		if(curproc->mmaps[i].size == 0) { // Assuming size 0 means the entry is unused

			mmap_entry = &curproc->mmaps[i];

    		break;

		}
  	
	}

	if(mmap_entry == 0) {
    	
		return (void *) -1;
  	
	}

	if (!(flags & MAP_FIXED)) {
        // This is the place for finding a suitable address
        addrArg = (void *)MMAP_BASE;

        while (addrArg < (void *)KERNBASE) {

            if (!is_mapped(curproc->pgdir, addrArg, size)) {

                break; // Found a suitable address.

            }

            addrArg += PGSIZE; // Try the next page.
        }
        if (addrArg >= (void *)KERNBASE) {

            return (void *)-1; // No space available.

        }
    }

	int numPages = (size + PGSIZE - 1) / PGSIZE;

	if(!(flags & MAP_ANONYMOUS)) {

		curproc->ofile[fd]->off = 0;

	}

	// Allocate and map memory pages
	for(int i = 0; i < numPages; i++) {

		pte_t *pte = walkpgdir(curproc->pgdir, (char *)addrArg + i * PGSIZE, 0);

		if(flags & MAP_FIXED && pte && (*pte & PTE_P)) {

			char *physical_addr = P2V(PTE_ADDR(*pte));
			kfree(physical_addr);
			*pte = 0;

		}

		char* mem;

		if(flags & MAP_ANONYMOUS) {
		
			mem = kalloc();

			if(mem == 0) {

				for (int j = 0; j < i; j++) {
					
					char *prev_addr = (char *)addrArg + j * PGSIZE;
					pte_t *pte = walkpgdir(curproc->pgdir, prev_addr, 0);

					if (pte && (*pte & PTE_P)) {

						char *physical_addr = P2V(PTE_ADDR(*pte));
						kfree(physical_addr);
						*pte = 0;

					}

				}
				kfree(mem);
				return (void *) -1;
			}

			memset(mem, 0, PGSIZE);

		}
		else {

			mem = kalloc();

			if(mem == 0) {

				for (int j = 0; j < i; j++) {
					
					char *prev_addr = (char *)addrArg + j * PGSIZE;
					pte_t *pte = walkpgdir(curproc->pgdir, prev_addr, 0);

					if (pte && (*pte & PTE_P)) {

						char *physical_addr = P2V(PTE_ADDR(*pte));
						kfree(physical_addr);
						*pte = 0;

					}

				}
				kfree(mem);
				return (void *) -1;
			}

			// Read the file content into the memory
			if(curproc->ofile[fd]->type == FD_INODE) {

				//curproc->ofile[fd]->off = 0;
				fileread(curproc->ofile[fd], mem, PGSIZE);

			}
			else {

				for (int j = 0; j < i; j++) {
					
					char *prev_addr = (char *)addrArg + j * PGSIZE;
					pte_t *pte = walkpgdir(curproc->pgdir, prev_addr, 0);

					if (pte && (*pte & PTE_P)) {

						char *physical_addr = P2V(PTE_ADDR(*pte));
						kfree(physical_addr);
						*pte = 0;

					}

				}
			
				kfree(mem);
				return (void *)-1;
			}

		}
		
		int perm = PTE_U; // User permission
		if (prot & PROT_READ) {
			perm |= PTE_P; // Present
		}
		if (prot & PROT_WRITE) {
			perm |= PTE_W; // Writable
		}

		if(mappages(curproc->pgdir, (char*)addrArg + i * PGSIZE, PGSIZE, V2P(mem), perm) < 0) {

			kfree(mem);
			return (void *) -1;

		}

	}

	// Update the mmap_region entry
	mmap_entry->virt_addr = (uint)addrArg;
	mmap_entry->size = numPages * PGSIZE;
	mmap_entry->flags = flags;
	mmap_entry->protection = prot;
	mmap_entry->f = (fd != -1) ? curproc->ofile[fd] : 0; // Assuming -1 means no file
	mmap_entry->offset = offset;

	// Increase total_mmaps count
	curproc->total_mmaps++;

	// Return the address of the mapped memory.
	return addrArg;

}
