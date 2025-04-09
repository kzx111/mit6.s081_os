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
  char name[8];
} kmem[NCPU];

void
kinit()
{
  for(int i=0;i<NCPU;i++){
    snprintf(kmem[i].name,8,"kmem_%d",i);
    initlock(&kmem[i].lock, kmem[i].name);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int id=cpuid();
  pop_off();

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}


struct run* steal(int id){
  struct run *r, *slow, *fast;
  if(id!=cpuid()){
    panic("steal");
  }
  for(int i=0;i<NCPU;i++){
    if(i==id)continue;
    acquire(&kmem[i].lock);
    r=kmem[i].freelist;
    if(r){
      slow=r;
      fast=slow->next;
      while(fast){
        fast=fast->next;
        if(fast){
          slow=slow->next;
          fast=fast->next;
        }
      }

      kmem[i].freelist=slow->next;
      release(&kmem[i].lock);
      slow->next=0;
      return r;
    }
    release(&kmem[i].lock);

  }
      // 若其他CPU物理页均为空则返回空指针
  return 0;
}




// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;


  push_off();
  int id=cpuid();
  pop_off();


  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;           //这里是r->next是因为要分配第一个所以要下一个
  release(&kmem[id].lock);


  if(!r){
    r=steal(id);
    if(r){
      acquire(&kmem[id].lock);
      kmem[id].freelist=r->next;
      release(&kmem[id].lock);
    }

  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
