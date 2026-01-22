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
#define BIGBUF  (NBUCKET*3)  // size of disk block cache

struct {
  struct spinlock lock;
  struct buf buf[BIGBUF];
} bcache;   



struct hcache{
  struct buf head;
  struct spinlock lock;
} hcache[NBUCKET];  
//所有cpu共享bcache，只有一个全局锁bcache.lock，cpu都会竞争这个锁

int hash(uint blockno)
{
  return (int)blockno%NBUCKET;
}

void
binit(void)
{
  struct buf *b;
  initlock(&bcache.lock,"bcache");
  for(int i=0;i<BIGBUF;i++)
  {
     initsleeplock(&bcache.buf[i].lock, "buffer");
  }
  b=bcache.buf;
  for(int i=0;i<NBUCKET;i++)
  {
    initlock(&hcache[i].lock, "bcache_hash");
    for(int j=0;j<BIGBUF/NBUCKET;j++){
      b->blockno=i;
      b->next=hcache[i].head.next;
      hcache[i].head.next=b;
      b->utime=0;
      b++;
    }
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int idx=hash(blockno);
  acquire(&hcache[idx].lock);
  struct  hcache*bucket=hcache+idx;
  
  //A 找的到的情况
  for(b=bucket->head.next;b!=0;b=b->next)
  {
     if(b->dev == dev && b->blockno == blockno)
      {
        b->refcnt++;
        release(&bucket->lock);
        acquiresleep(&b->lock);
        return b;
      }
  }

  //B 没找的到的情况
  //先在自己的bucket里面找least used buf
  struct buf *replace_buf=0;
  uint mintime=__INT32_MAX__;
  for(b=bucket->head.next;b!=0;b=b->next)
  {
       static uint mintime=__INT32_MAX__;
       if((b->utime<mintime)&&(b->refcnt==0))
       {
        replace_buf=b;
        mintime=b->utime;
       }
  }
  if (replace_buf)goto find;
  //自己家bucket没找到，去其它bucket偷
  
  acquire(&bcache.lock);

  refind:
  mintime=__INT32_MAX__;
  for(b=bcache.buf;b<bcache.buf+BIGBUF;b++)
  {
      if((b->utime<mintime)&&(b->refcnt==0))
      {
        replace_buf=b;
        mintime=b->utime;
      }
  }
 
  /*全局数组中找到一个块之后，到对该桶加上锁之间有一个窗口，
  可能就在这个窗口里面这个块就被那个桶对应的本地查找阶段用掉了。
  因此，需要在加上锁之后判断是否被用了，如果被用了就要重新查找
  */
  if(replace_buf)
  {
    int replace_idx=hash(replace_buf->blockno);
    if(replace_idx != idx){   //同一个桶：你已经持有 bucket->lock，不要重复 acquire
      acquire(&hcache[replace_idx].lock);
    }
    if(replace_buf->refcnt != 0)  // be used in another bucket's local find between finded and acquire
    {
      if(replace_idx != idx){   
        release(&hcache[replace_idx].lock);
      }
    
      goto refind;
    }

    struct buf *pre=&hcache[replace_idx].head;
    struct buf *next=hcache[replace_idx].head.next;
    while(next!=replace_buf)
    {
      pre=next;
      next=next->next;
    }
    pre->next=replace_buf->next;
      if(replace_idx != idx){  
      release(&hcache[replace_idx].lock);
    }

    replace_buf->next=bucket->head.next;
    bucket->head.next=replace_buf;
    release(&bcache.lock);   //replace_buf的操作结束，释放bcache锁
  }
  else {
    panic("bget: no buffers");
  }


  find:
      replace_buf->dev = dev;
      replace_buf->blockno = blockno;
      replace_buf->valid = 0;
      replace_buf->refcnt = 1;
      release(&bucket->lock);
      acquiresleep(&replace_buf->lock);
      return replace_buf;

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

  int idx=hash(b->blockno);

  acquire(&hcache[idx].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->utime = ticks;
  }
  release(&hcache[idx].lock);
}

void
bpin(struct buf *b) {
  int idx=hash(b->blockno);
  acquire(&hcache[idx].lock);
  b->refcnt++;
  release(&hcache[idx].lock);
}

void
bunpin(struct buf *b) {
  int idx=hash(b->blockno);
  acquire(&hcache[idx].lock);
  b->refcnt--;
  release(&hcache[idx].lock);
}


