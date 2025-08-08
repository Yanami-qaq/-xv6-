// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// 外部变量声明
extern uint ticks;

// 哈希桶数量，使用质数减少冲突
#define NBUCKET 13

struct bucket {
  struct spinlock lock;
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
};

struct {
  struct buf buf[NBUF];
  struct bucket bucket[NBUCKET];
} bcache;

static uint hash_v(uint dev, uint blockno) {
  return (dev + blockno) % NBUCKET;
}

static void initbucket(struct bucket* b) {
  initlock(&b->lock, "bcache.bucket");
  b->head.prev = &b->head;
  b->head.next = &b->head;
}

void
binit(void)
{
  struct buf *b;

  // 初始化所有缓冲区
  for (int i = 0; i < NBUF; ++i) {
    b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->refcnt = 0;
    b->timestamp = 0;
  }

  // 初始化所有哈希桶
  for (int i = 0; i < NBUCKET; ++i) {
    initbucket(&bcache.bucket[i]);
  }

  // 将所有缓冲区添加到第一个桶中
  for (int i = 0; i < NBUF; ++i) {
    b = &bcache.buf[i];
    b->next = bcache.bucket[0].head.next;
    b->prev = &bcache.bucket[0].head;
    bcache.bucket[0].head.next->prev = b;
    bcache.bucket[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  uint v = hash_v(dev, blockno);
  struct bucket* bucket = &bcache.bucket[v];
  acquire(&bucket->lock);

  // Is the block already cached?
  for (struct buf *buf = bucket->head.next; buf != &bucket->head;
       buf = buf->next) {
    if(buf->dev == dev && buf->blockno == blockno){
      buf->refcnt++;
      buf->timestamp = ticks;
      release(&bucket->lock);
      acquiresleep(&buf->lock);
      return buf;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for (struct buf *buf = bucket->head.prev; buf != &bucket->head;
       buf = buf->prev) {
    if(buf->refcnt == 0) {
      buf->dev = dev;
      buf->blockno = blockno;
      buf->valid = 0;
      buf->refcnt = 1;
      buf->timestamp = ticks;

      // Move to head of list
      buf->next->prev = buf->prev;
      buf->prev->next = buf->next;
      buf->next = bucket->head.next;
      buf->prev = &bucket->head;
      bucket->head.next->prev = buf;
      bucket->head.next = buf;

      release(&bucket->lock);
      acquiresleep(&buf->lock);
      return buf;
    }
  }

  // 当前桶中没有未使用的缓冲区，需要从其他桶中驱逐一个
  release(&bucket->lock);
  
  // 在所有桶中查找最久未使用的缓冲区
  struct buf *oldest = 0;
  uint oldest_ts = 0xffffffff;
  int oldest_bucket = -1;
  
  for(int i = 0; i < NBUCKET; i++) {
    if(i == v) continue; // 跳过当前桶，避免死锁
    acquire(&bcache.bucket[i].lock);
    for(struct buf *buf = bcache.bucket[i].head.prev; buf != &bcache.bucket[i].head; buf = buf->prev) {
      if(buf->refcnt == 0 && buf->timestamp < oldest_ts) {
        oldest = buf;
        oldest_ts = buf->timestamp;
        oldest_bucket = i;
      }
    }
    release(&bcache.bucket[i].lock);
  }
  
  if(!oldest) {
    panic("bget: no buffers");
  }
  
  // 从原桶中移除最旧的缓冲区
  acquire(&bcache.bucket[oldest_bucket].lock);
  oldest->next->prev = oldest->prev;
  oldest->prev->next = oldest->next;
  release(&bcache.bucket[oldest_bucket].lock);
  
  // 将缓冲区添加到目标桶
  acquire(&bucket->lock);
  oldest->next = bucket->head.next;
  oldest->prev = &bucket->head;
  bucket->head.next->prev = oldest;
  bucket->head.next = oldest;
  oldest->dev = dev;
  oldest->blockno = blockno;
  oldest->valid = 0;
  oldest->refcnt = 1;
  oldest->timestamp = ticks;
  release(&bucket->lock);
  
  acquiresleep(&oldest->lock);
  return oldest;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint v = hash_v(b->dev, b->blockno);
  struct bucket* bucket = &bcache.bucket[v];
  acquire(&bucket->lock);

  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bucket->head.next;
    b->prev = &bucket->head;
    bucket->head.next->prev = b;
    bucket->head.next = b;
  }
  b->timestamp = ticks;
  
  release(&bucket->lock);
}

void
bpin(struct buf *b) {
  uint v = hash_v(b->dev, b->blockno);
  struct bucket* bucket = &bcache.bucket[v];
  acquire(&bucket->lock);
  b->refcnt++;
  release(&bucket->lock);
}

void
bunpin(struct buf *b) {
  uint v = hash_v(b->dev, b->blockno);
  struct bucket* bucket = &bcache.bucket[v];
  acquire(&bucket->lock);
  b->refcnt--;
  release(&bucket->lock);
}