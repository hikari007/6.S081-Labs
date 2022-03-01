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

#define NBUCKET 13

struct {
  struct spinlock cachelock;
  struct spinlock lock[NBUCKET];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NBUCKET];
} bcache;

void
binit(void)
{
  struct buf *b;
  int n;

  initlock(&bcache.cachelock, "bcache");
  for (int i = 0; i < NBUCKET; i++) 
    initlock(&bcache.lock[i], "bcache");


  // Create linked list of buffers
  for(b = bcache.buf, n = 0; b < bcache.buf+NBUF; b++, n++){
    int k = n % NBUCKET;
    b->timestamp = 0;
    b->next = bcache.head[k].next;
    bcache.head[k].next = b;
    initsleeplock(&b->lock, "buffer");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct buf *r = 0;

  int id = blockno % NBUCKET;

  acquire(&bcache.lock[id]);

  // Is the block already cached?
  for(b = bcache.head[id].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  uint mntime = -1;
  for(b = bcache.head[id].next; b; b = b->next){
    if(b->refcnt == 0 && b->timestamp < mntime) {
      mntime = b->timestamp;
      r = b;
    }
  }
  if (r) 
    goto find;

  acquire(&bcache.cachelock);
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    if (b->refcnt == 0 && b->timestamp < mntime) {
      mntime = b->timestamp;
      r = b;
    }
  }
  if (r) {
    int rid = r->blockno % NBUCKET;
    acquire(&bcache.lock[rid]);
    struct buf *prev = &bcache.head[rid];
    while (prev->next != r)
      prev = prev->next;
    prev->next = r->next;
    release(&bcache.lock[rid]);
    r->next = bcache.head[id].next;
    bcache.head[id].next = r;
    release(&bcache.cachelock);
    goto find;
  }
  else {
    panic("bget: no buffers");
  }

  find:
    r->dev = dev;
    r->blockno = blockno;
    r->valid = 0;
    r->refcnt = 1;
    release(&bcache.lock[id]);
    acquiresleep(&r->lock);
    return r;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;
  
  b = bget(dev, blockno);
  b->timestamp = ticks;
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
  b->timestamp = ticks;
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");
  b->refcnt--;
  releasesleep(&b->lock);
}

void
bpin(struct buf *b) {
  int id = b->blockno % NBUCKET;
  acquire(&bcache.lock[id]);
  b->refcnt++;
  release(&bcache.lock[id]);
}

void
bunpin(struct buf *b) {
  int id = b->blockno % NBUCKET;
  acquire(&bcache.lock[id]);
  b->refcnt--;
  release(&bcache.lock[id]);
}


