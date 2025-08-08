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

// 每CPU内存分配器结构
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  // 为每个CPU初始化锁和空闲列表
  for(int i = 0; i < NCPU; i++) {
    char lockname[16];
    snprintf(lockname, sizeof(lockname), "kmem%d", i);
    initlock(&kmem[i].lock, lockname);
    kmem[i].freelist = 0;
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

  // 获取当前CPU ID，需要关闭中断
  push_off();
  int cpu = cpuid();
  pop_off();

  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);
}

// 从其他CPU偷取内存页面的函数
static struct run*
steal_pages(int cpu)
{
  struct run *r = 0;
  
  // 遍历所有其他CPU，尝试偷取页面
  for(int i = 0; i < NCPU; i++) {
    if(i == cpu) continue;
    
    acquire(&kmem[i].lock);
    if(kmem[i].freelist) {
      // 计算当前CPU有多少页面
      struct run *current = kmem[i].freelist;
      int count = 0;
      while(current) {
        count++;
        current = current->next;
      }
      
      // 偷取一半的页面，至少偷取1个
      int steal_count = count / 2;
      if(steal_count == 0 && count > 0) {
        steal_count = 1;
      }
      
      if(steal_count > 0) {
        // 找到分割点
        current = kmem[i].freelist;
        struct run *prev = 0;
        for(int j = 0; j < steal_count - 1; j++) {
          prev = current;
          current = current->next;
        }
        
        // 分割列表
        if(prev) {
          r = kmem[i].freelist;
          kmem[i].freelist = current->next;
          current->next = 0;
        } else {
          r = kmem[i].freelist;
          kmem[i].freelist = 0;
        }
      }
    }
    release(&kmem[i].lock);
    
    if(r) break; // 如果成功偷取了页面，就退出循环
  }
  
  return r;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // 获取当前CPU ID，需要关闭中断
  push_off();
  int cpu = cpuid();
  pop_off();

  acquire(&kmem[cpu].lock);
  r = kmem[cpu].freelist;
  if(r)
    kmem[cpu].freelist = r->next;
  release(&kmem[cpu].lock);

  // 如果当前CPU没有可用页面，尝试从其他CPU偷取
  if(!r) {
    struct run *stolen = steal_pages(cpu);
    if(stolen) {
      // 将偷取的页面添加到当前CPU的列表中
      acquire(&kmem[cpu].lock);
      // 找到偷取列表的最后一个页面
      struct run *last = stolen;
      while(last->next) {
        last = last->next;
      }
      // 将偷取的页面添加到当前CPU的列表头部
      last->next = kmem[cpu].freelist;
      kmem[cpu].freelist = stolen;
      
      // 现在从当前CPU的列表中分配一个页面
      r = kmem[cpu].freelist;
      kmem[cpu].freelist = r->next;
      release(&kmem[cpu].lock);
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
