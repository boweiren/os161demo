#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <limits.h>
#include <syscall.h>
#include <vfs.h>
#include <vnode.h>
#include <uio.h>
#include <kern/fcntl.h>
#include <kern/stat.h>
#include <elf.h>
#include <bitmap.h>
#include <synch.h>
#include <syscall.h>

/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

/*
 * Global variables
 */
struct coremap *coremap;     // use coremap array
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;
int coremap_init = 0;

// number of pages in coremap, will be set in vm_bootstrap
static unsigned int num_pages;

paddr_t freepaddr; // next free physical address

// variables for swap disk
struct vnode *swap_disk;
struct lock *swap_disk_lock;
struct bitmap *swap_disk_bitmap;

/*
 * Initialize the coremap at the beginning
 */
void 
vm_bootstrap(void){ 
    kprintf("vm_bootstrap\n");
    int err;
    paddr_t firstpaddr, lastpaddr, freepaddr;
    struct stat swap_disk_stat;

    // open swap disk
    // the file name must be lhd0raw: or lhd1raw 
    // and the open flag must be O_RDWR
    char diskname[16];
    strcpy(diskname, "lhd0raw:");
    err = vfs_open(diskname, O_RDWR, 0664, &swap_disk); //didn't find O_RDWR in other files. declared in vm.h
    if (err) {
        panic("Open swap disk failed\n");
    }

    // initialize the swap disk bitmap and lock
    VOP_STAT(swap_disk, &swap_disk_stat);
    swap_disk_bitmap = bitmap_create(swap_disk_stat.st_size / PAGE_SIZE);
    swap_disk_lock = lock_create("swap_disk_lock");
    //kprintf("swap disk initialized\n");

    // read the available ram address range for initialization
    // firstpaddr = ram_getfirstfree();
    lastpaddr = ram_getsize();

    // calculate the number of pages in the coremap
    num_pages = lastpaddr / PAGE_SIZE;

    // calculate the size of coremap
    size_t coremap_size = ROUNDUP(num_pages * sizeof(struct coremap), PAGE_SIZE) / PAGE_SIZE;

    // steal physical memory for coremap
    spinlock_acquire(&stealmem_lock);
    paddr_t coremap_addr = ram_stealmem(coremap_size);
    spinlock_release(&stealmem_lock);
    
    // initialize the coremap
    coremap = (struct coremap*) PADDR_TO_KVADDR(coremap_addr);
    bzero(coremap, coremap_size * PAGE_SIZE);
    //kprintf("coremap initialized\n");

    // initialize the free pages
    firstpaddr = ram_getfirstfree();
    freepaddr = firstpaddr;
    void *freespace = (void *) PADDR_TO_KVADDR(freepaddr);
    bzero(freespace, lastpaddr - freepaddr);
    //kprintf("free pages initialized\n");

    /* 
    * initialize the coremap array
    * the whole ram should look like this:
    * ------<ram begin>------
    *           |
    *   [something dirty]
    *           |
    * ------<firstpaddr>------
    *           |
    *   [coremap array]
    *           |
    * ------<nextpaddr>------
    *           |
    *   [some free space]
    *           |
    * ------<lastpaddr>(<ram end>)------
    * 
    * we can convert physicall address to coremap index using:
    * index = (paddr & PAGE_FRAME) / PAGE_SIZE
    */
    unsigned int num_dirty_pages = (firstpaddr & PAGE_FRAME) / PAGE_SIZE;
    // unsigned long num_fixed_pages = ((freepaddr - firstpaddr) & PAGE_FRAME) / PAGE_SIZE;
    unsigned int num_free_pages = (freepaddr & PAGE_FRAME) / PAGE_SIZE;
    // stolen pages are marked as dirty
    for(unsigned int i = 0; i < num_dirty_pages; i++){
        paddr_t paddr = freepaddr + (i * PAGE_SIZE);
        coremap[i].as = NULL;
        coremap[i].size = 1;
        coremap[i].state = PG_DIRTY;
        coremap[i].vaddr = PADDR_TO_KVADDR(paddr);
        coremap[i].kernel = 1;
    }

    // coremap pages are marked as fixed
    for(unsigned int i = num_dirty_pages; i < num_free_pages; i++){
        paddr_t paddr = freepaddr + (i * PAGE_SIZE);
        coremap[i].as = NULL;
        coremap[i].size = 1;
        coremap[i].state = PG_FIXED;
        coremap[i].vaddr = PADDR_TO_KVADDR(paddr);
        coremap[i].kernel = 1;
    }

    // remaining pages are marked as free
    for(unsigned int i = num_free_pages; i < num_pages; i++){
        paddr_t paddr = freepaddr + (i * PAGE_SIZE);
        coremap[i].as = NULL;
        coremap[i].size = 0;
        coremap[i].state = PG_FREE;
        coremap[i].vaddr = PADDR_TO_KVADDR(paddr);
        coremap[i].kernel = 0;
    }

    coremap_init = 1;
}

/*
 * Helper function adapted from dumbvm implementation
 * used to find contiguous free pages on the physical memory
 */
static 
paddr_t getppages(unsigned long npages){
	paddr_t addr;

    if (coremap_init) {
        // do something smart
        spinlock_acquire(&coremap_lock);

        unsigned long count;
        unsigned long i = 0;
        unsigned long start_index;

        // try to find a big enough contiguous space
        while (i < num_pages) {
            if (count == npages) {
                break;
            } else if (coremap[i].state == PG_FREE) {
                count++;
            } else {
                count = 0;
                start_index = i + 1;
            }
            i++;
        }

        // if success
        if (count == npages) {
            void *start_addr = (void *) PADDR_TO_KVADDR(start_index * PAGE_SIZE);
            bzero(start_addr, npages * PAGE_SIZE);

            for (i = 0; i < npages; i++) {
                // only record the size at first entry
                if (i == 0) {
                    coremap[start_index + i].size = (size_t) npages;
                } else {
                    coremap[start_index + i].size = 0;
                }
                coremap[start_index + i].state = PG_FIXED;
                coremap[start_index + i].as = NULL;
                coremap[start_index + i].vaddr = PADDR_TO_KVADDR((start_index + i) * PAGE_SIZE);
                coremap[start_index + i].kernel = 1;
            }

            addr = start_index * PAGE_SIZE;
        } else {
            panic("getppages: out of memory\n");
        }

        spinlock_release(&coremap_lock);
    } else {
        // do dumb things if coremap is not initialized
        spinlock_acquire(&stealmem_lock);
	    addr = ram_stealmem(npages);
	    spinlock_release(&stealmem_lock);
    }
	return addr;
}

int 
vm_fault(int faulttype, vaddr_t faultaddress){ 
    return (int) faulttype + faultaddress; 
}

vaddr_t 
alloc_kpages(unsigned npages){ 
    paddr_t paddr = getppages(npages);
    return PADDR_TO_KVADDR(paddr); 
}

void 
free_kpages(vaddr_t addr){ 
    // should not be called to free kernel pages
    KASSERT(addr >= MIPS_KSEG0);

    // calculate the absolute physical address
    paddr_t paddr = addr - MIPS_KSEG0;

    // calculate the index of coremap
    unsigned int index = (paddr & PAGE_FRAME) / PAGE_SIZE;

    spinlock_acquire(&coremap_lock);

    // free the pages
    size_t size = coremap[index].size;
    for (unsigned int i = index; i < size + index; i++) {
        coremap[i].state = PG_FREE;
        coremap[i].as = NULL;
        coremap[i].vaddr = 0;
        coremap[i].kernel = 0;
        coremap[i].size = 0;
    }
    bzero((void *) addr, PAGE_SIZE * size);

    spinlock_release(&coremap_lock);
}

void 
vm_tlbshootdown_all(void){ 
    return; 
}

void 
vm_tlbshootdown(const struct tlbshootdown * ts){ 
    (void) ts; 
}