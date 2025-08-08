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
} kmem;

// 引用计数数组，使用uint8来减少内存占用
// 每个页面用1个字节存储引用计数，最大255个引用
// PHYSTOP = KERNBASE + 128MB，总页面数 = 128MB / 4KB = 32768
#define REF_COUNT_SIZE 32768
uint8 ref_count[REF_COUNT_SIZE];

// 获取物理地址对应的引用计数索引
static inline int
pa_to_ref_index(uint64 pa)
{
  if(pa < KERNBASE || pa >= PHYSTOP)
    return -1;
  return (pa - KERNBASE) / PGSIZE;
}

// 增加页面引用计数
void
krefpage(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (uint64)pa < KERNBASE || (uint64)pa >= PHYSTOP)
    return;
  
  int index = pa_to_ref_index((uint64)pa);
  if(index >= 0 && index < REF_COUNT_SIZE) {
    acquire(&kmem.lock);
    if(ref_count[index] < 255)  // 防止溢出
      ref_count[index]++;
    release(&kmem.lock);
  }
}

// 减少页面引用计数
void
kunrefpage(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (uint64)pa < KERNBASE || (uint64)pa >= PHYSTOP)
    return;
  
  int index = pa_to_ref_index((uint64)pa);
  if(index >= 0 && index < REF_COUNT_SIZE) {
    acquire(&kmem.lock);
    if(ref_count[index] > 0) {
      ref_count[index]--;
      // 如果引用计数变为0，释放页面
      if(ref_count[index] == 0) {
        // 清零引用计数
        ref_count[index] = 0;
        release(&kmem.lock);
        
        // 直接释放页面到空闲列表
        struct run *r = (struct run*)pa;
        memset(pa, 1, PGSIZE); // Fill with junk
        
        acquire(&kmem.lock);
        r->next = kmem.freelist;
        kmem.freelist = r;
        release(&kmem.lock);
        return;
      }
    }
    release(&kmem.lock);
  }
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  // 初始化引用计数数组
  memset(ref_count, 0, sizeof(ref_count));
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

  // 检查引用计数，只有当引用计数为0时才真正释放页面
  int index = pa_to_ref_index((uint64)pa);
  if(index >= 0 && index < REF_COUNT_SIZE) {
    acquire(&kmem.lock);
    if(ref_count[index] > 0) {
      // 还有引用，不释放
      release(&kmem.lock);
      return;
    }
    // 引用计数为0，清零引用计数（防止重复清零）
    ref_count[index] = 0;
    release(&kmem.lock);
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
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
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    // 新分配的页面初始引用计数为1，表示被当前进程拥有
    int index = pa_to_ref_index((uint64)r);
    if(index >= 0 && index < REF_COUNT_SIZE) {
      acquire(&kmem.lock);
      ref_count[index] = 1;
      release(&kmem.lock);
    }
  }

  return (void*)r;
}


