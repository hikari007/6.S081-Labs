// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  int refcnt[(PHYSTOP - KERNBASE) / PGSIZE];
} kmem;

inline int
pageindex(uint64 pa)
{
  return (pa - KERNBASE) / PGSIZE;
}

inline int
getrefcnt(uint64 pa)
{
  int cnt;
  acquire(&kmem.lock);
  cnt = kmem.refcnt[pageindex(pa)];
  release(&kmem.lock);

  return cnt; 
}

inline void
refincr(uint64 pa)
{
  acquire(&kmem.lock);
  kmem.refcnt[pageindex(pa)] += 1;
  release(&kmem.lock);
}

inline void
refdecr(uint64 pa)
{
  acquire(&kmem.lock);
  kmem.refcnt[pageindex(pa)] -= 1;
  release(&kmem.lock);
}


void
kinit()
{
  initlock(&kmem.lock, "kmem");
  acquire(&kmem.lock);
  for (int i = 0; i < (PHYSTOP - KERNBASE) / PGSIZE; i++)
    kmem.refcnt[i] = 1;
  release(&kmem.lock);

  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);
  // 引用计数减为0时，将物理页加入空闲链表
  if (--kmem.refcnt[pageindex((uint64)pa)] == 0) {
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;
    r->next = kmem.freelist;
    kmem.freelist = r;
  }
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    kmem.refcnt[pageindex((uint64)r)] = 1;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  return (void*)r;
}
