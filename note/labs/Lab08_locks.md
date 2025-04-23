# lab8 : locks

## Memory allocator

​		kalloctest 打印由于尝试获取另一个内核已经持有的锁而导致的获取中的循环迭代次数，包括kmem锁和其他一些锁。捕获中的循环迭代次数是锁争用的粗略度量。acquire为每个锁维护该锁的调用计数，以及acquire中的循环尝试但未能设置锁的次数。kalloctest调用一个系统调用，使内核打印kmem和bcache锁（这是本实验室的重点）以及5个竞争最激烈的锁的计数。如果存在锁争用，则获取循环迭代的次数将很大。

![](..\pic\locktest.png)



kmem中acquire调用433070次其中36575次是尝试获取锁但是失败的，tot 是lock kmem和lock bcache 尝试获得锁但是失败的次数。



kalloctest中锁争用的**根本原因**是kalloc（）有一个freelist，由一个锁保护。您必须重新设计内存分配器，以避免出现单个锁和列表。**基本思想**是为每个CPU维护一个空闲列表，每个列表都有自己的锁。不同CPU上的分配和释放可以并行运行，因为每个CPU将在不同的列表上运行。主要的挑战将是处理一个CPU的空闲列表为空，但另一个CPU列表有空闲内存的情况；在这种情况下，一个CPU必须“窃取”另一个CPU的空闲列表的一部分。窃取可能会引入锁争用，但希望这种情况很少发生。

> 你的工作是实现每个CPU的空闲列表，并在CPU的空闲表为空时进行窃取。您必须为所有以“kmem”开头的锁命名。也就是说，您应该为每个锁调用initlock，并传递一个以“kmem”开头的名称。运行kalloctest，查看您的实现是否减少了锁争用。要检查它是否仍然可以分配所有内存，请运行usertests sbrkmuch。

提示：

* 使用NCPU
* 让freerange将空闲内存给到正在运行freerange的CPU
* 函数 *cpuid()* 返回当前的 *cpu* 编号， 不过需要关闭中断才能保证该函数被安全地使用。中断开关可使用 *push_off()* 和 *pop_off()*
* 用 *kmem* 命名你的锁 ，看看kernel/sprintf.c中的snprintf函数，有利于字符串格式化

在网上看的，这里有两种steal的方法，

第一个，[xiaofan](https://fanxiao.tech/posts/2021-03-02-mit-6s081-notes/)中在有空闲的内存池中steal第一个空闲内存

第二个，[这篇博客](https://blog.csdn.net/LostUnravel/article/details/121430900)就是直接在其他的内存池中获取一半的空闲内存，**采用了"快慢双指针"的算法来得到链表的中间结点**（向上取整 3个slow指向第二个）







语法细节：

 struct run* r,* slow,* fast;正确

r和slow fast都为指针

而 struct run* r,slow,fast;

则r为指针，slow，fast为结构体。





## Buffer cache

​		



​		**修改块缓存**，以便在运行bcachetest时，bcache中所有锁的获取循环迭代次数接近零。理想情况下，块缓存中涉及的所有锁的计数之和应为零，但如果总和小于500，则可以。**修改bget和brelse**，这样bcache中不同块的并发查找和释放就不太可能在锁上发生冲突（例如，不必都等待bcache.lock）。您必须保持不变，即每个块最多缓存一个副本。

​		减少块缓存中的争用比kalloc更棘手，因为bcache缓冲区真正在进程（以及CPU）之间共享。对于kalloc，可以通过给每个CPU分配自己的分配器来消除大部分争用；这对块缓存不起作用。我们建议您使用**每个哈希桶**都有锁的哈希表在缓存中查找块号。
​		在某些情况下，如果您的解决方案存在锁冲突，这是可以的：

* 当两个进程同时使用相同的块号时。bcachetest test0从不这样做。
* 当两个进程同时在缓存中丢失时，需要找到一个未使用的块进行替换。bcachetest test0从不这样做。
* 当两个进程同时使用在您用于划分块和锁的任何方案中冲突的块时；例如，如果两个进程使用块号哈希到哈希表中相同槽位的块。bcachetesttest0可能会这样做，具体取决于您的设计，但您应该尝试调整方案的细节以避免冲突（例如，更改哈希表的大小）。

​		提示：

* 阅读xv6书中关于块缓存的描述（第8.1-8.3节）。
* 使用固定数量的bucket并且不动态调整哈希表的大小是可以的。使用`NBUCKET`最好选择素数（例如13）来降低哈希冲突的可能性。
* 在哈希表中搜索缓冲区，并在找不到缓冲区时为该缓冲区分配条目，必须是原子性的。
* 删除所有缓冲区（bcache.head等）的列表，改用使用上次使用时间的时间戳缓冲区（即在kernel/trap.c中使用刻度）。通过此更改，brelse不需要获取bcache锁，bget可以根据时间戳选择的least-recently used block 。
* 在bget中序列化驱逐是可以的（即，当缓存中的查找失败时，bget中选择要重用的缓冲区的部分）。
* 在某些情况下，您的解决方案可能需要持有两把锁；例如，在驱逐过程中，您可能需要持有bcache锁和每个bucket一个锁。确保你避免死锁。
* 替换块时，您可能会将结构buf从一个bucket移动到另一个buckets，因为新块哈希到不同的bucket。你可能会遇到一个棘手的情况：新区块可能会哈希到与旧块相同的桶中。在这种情况下，一定要避免陷入僵局。
* 一些调试技巧：实现桶锁，但在bget的开始/结束处保留全局bcache.lock acquire/release，以序列化代码。一旦您确定它在没有竞争条件的情况下是正确的，请删除全局锁并处理并发问题。您还可以运行make CPUS=1 qemu来测试一个内核。

​		

[xiaofan](https://fanxiao.tech/posts/2021-03-02-mit-6s081-notes/)这里分析了死锁的情况，首先分析一个进程中，然后分析两个进程中死锁的情况（破会循环等待条件）

原话：

> 如果没有找到对应的`buf`，需要在整个哈希表中查找LRU(least recently used)`buf`，将其替换掉。这里由于总共有`NBUCKET`个哈希表，而此时一定是持有`bcache.lock[id]`这个哈希表的锁的，因此当查找其他哈希表时，需要获取其他哈希表的锁，这时就会有产生死锁的风险。风险1：查找的哈希表正是自己本身这个哈希表，在已经持有自己哈希表锁的情况下，不能再尝试`acquire`一遍自己的锁。风险2：假设有2个进程同时要进行此步骤，进程1已经持有了哈希表A的锁，尝试获取哈希表B的锁，进程2已经持有了哈希表B的锁，尝试获取哈希表A的锁，同样会造成死锁，因此要规定一个规则，是的当持有哈希表A的情况下如果能够获取哈希表B的锁，则当持有哈希表B锁的情况下不能够持有哈希表A的锁。该规则在`can_lock`函数中实现。

![](..\pic\deadlock_lab8-2.JPG)



这个函数就是判断是否可以获取为了一个哈希的锁

```C

int can_lock(int id,int j){
  int mid=NBUCKET/2;
  if(id<=mid){
    if(j>id&&j<=(id+mid))
    return 0;
  }else{
    if((j>id&&j<NBUCKET)||(j<=(id+mid)%NBUCKET))
    return 0;
  }
  return 1;
}

```

接下来就是将之前的缓存结构改成哈希表的形式

```c
struct {
  struct spinlock lock[NBUCKET];
  struct buf buf[NBUF];
  struct buf head[NBUCKET];
} bcache;
```

还有利用时钟中断的ticks来判断1哪一个是LRU

一个非常关键的地方是bget的地方，

```c
  int index = -1;
  uint smallest_tick = __UINT32_MAX__;
    for(int j=0;j<NBUCKET;j++){
      if(j!=i&&can_lock(i,j)){
        acquire(&bcache.lock[j]);
      }else if(!can_lock(i,j)){
        continue;
      }
      b=bcache.head[j].next;
      while(b){
        if(b->refcnt==0){
           if (b->time < smallest_tick) {
                smallest_tick = b->time;
                if (index != -1 && index != j && holding(&bcache.lock[index])) release(&bcache.lock[index]);
                index = j;
            }   
        }
        b=b->next;
      }
       if (j!=i && j!=index && holding(&bcache.lock[j])) release(&bcache.lock[j]);
}
if(index==-1)panic("bget:no buffers");
b=&bcache.head[index];
while (b->next) {
    if ((b->next)->refcnt == 0 && (b->next)->time == smallest_tick) {
        selected = b->next;
        b->next = b->next->next;
        break;
    }
    b = b->next;
}
if(i!=index && holding(&bcache.lock[index])){
  release(&bcache.lock[index]);
}
b=&bcache.head[i];
while(b->next){
  b=b->next;
}
b->next=selected;
selected->next=0;
selected->dev=dev;
selected->blockno=blockno;
selected->valid=0;
selected->refcnt=1;
if (holding(&bcache.lock[i]))
    release(&bcache.lock[i]);
acquiresleep(&selected->lock);
return selected;
```

这个地方我思考了好久，是关于在自己的哈希链表中没有找到对应的缓冲区就去其他的哈希结构中找，

其他的哈希结构中找到最小的缓冲区，该缓存区所在的组记录在idex中，并不释放该哈希结构的锁，但是其他组的锁都释放了，

然后在该哈希结构中寻找我们所想找的缓冲区，找到并修改之后，我们无需对idex最小组哈希结构进行改变，所以要释放idex组哈希的锁，然后就是对i组的哈希结构进行操作，操作完则释放i组的锁（函数一开始就设立的锁。然后就是获取自己所要缓冲区的睡眠锁。

这段代码这段是硬骨头。

接下来就是对binit,brelse,bpin,bunpin这些函数的修改。还有一个细节就是NBUCKET的值修改为13质数，减小哈希冲突的可能性。







[csdn](https://blog.csdn.net/LostUnravel/article/details/121430900)中第二个实验就要简化了一点，没有像上一个那样考虑死锁，就简简单单看看bget函数

bcache

```c

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  int size;           // record the count of used buf - lab8-2
  struct buf buckets[NBUCKET];
  struct spinlock locks[NBUCKET];
  struct spinlock hashlock;

} bcache;
```





```c
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  // lab8-2
  int idx = HASH(blockno);
  struct buf *pre, *minb = 0, *minpre;
  uint mintimestamp;
  int i;
  
  // loop up the buf in the buckets[idx]
  acquire(&bcache.locks[idx]);  // lab8-2
  for(b = bcache.buckets[idx].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.locks[idx]);  // lab8-2
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // check if there is a buf not used -lab8-2
  acquire(&bcache.lock);
  if(bcache.size < NBUF) {
    b = &bcache.buf[bcache.size++];
    release(&bcache.lock);
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    b->next = bcache.buckets[idx].next;
    bcache.buckets[idx].next = b;
    release(&bcache.locks[idx]);
    acquiresleep(&b->lock);
    return b;
  }
  release(&bcache.lock);
  release(&bcache.locks[idx]);

  // select the last-recently used block int the bucket
  //based on the timestamp - lab8-2
  acquire(&bcache.hashlock);
  for(i = 0; i < NBUCKET; ++i) {
      mintimestamp = -1;
      acquire(&bcache.locks[idx]);
      for(pre = &bcache.buckets[idx], b = pre->next; b; pre = b, b = b->next) {
          // research the block
          if(idx == HASH(blockno) && b->dev == dev && b->blockno == blockno){
              b->refcnt++;
              release(&bcache.locks[idx]);
              release(&bcache.hashlock);
              acquiresleep(&b->lock);
              return b;
          }
          if(b->refcnt == 0 && b->timestamp < mintimestamp) {
              minb = b;
              minpre = pre;
              mintimestamp = b->timestamp;
          }
      }
      // find an unused block
      if(minb) {
          minb->dev = dev;
          minb->blockno = blockno;
          minb->valid = 0;
          minb->refcnt = 1;
          // if block in another bucket, we should move it to correct bucket
          if(idx != HASH(blockno)) {
              minpre->next = minb->next;    // remove block
              release(&bcache.locks[idx]);
              idx = HASH(blockno);  // the correct bucket index
              acquire(&bcache.locks[idx]);
              minb->next = bcache.buckets[idx].next;    // move block to correct bucket
              bcache.buckets[idx].next = minb;
          }
          release(&bcache.locks[idx]);
          release(&bcache.hashlock);
          acquiresleep(&minb->lock);
          return minb;
      }
      release(&bcache.locks[idx]);
      if(++idx == NBUCKET) {
          idx = 0;
      }
  }
  release(&bcache.hashlock);
  panic("bget: no buffers");
}
```

该博主初始化的时候并没有将缓冲区数组加入到哈希表中（上一个就是直接将所有的缓存块先置于一个 bucket 中(如 `buckets[0]`), 这样同样可以在这一步进行重用），老规矩，一开始就是获得blockno的哈希值(idx)，在idx对应的哈希bucket中找对应的缓存，若找到了，直接返回，若没有，且缓冲区大小小于NBUF，则将未加入到哈希表的缓冲区返回。

然后，若缓冲区都加入进来，则先将bcache.hashlock锁住，循环遍历其他的bucket，一开始，mintimestamp==-1,注意，**这里-1是最大值**，因为mintimestamp是无符号整型(uint)。

然后先从目标bucket(即 `idx=HASH(blockno)`)开始，遍历整个链表寻找引用数为0的，且timestamp最小的缓冲区，如果知道了就修改该缓冲区的值并返回，

如果没有找到就找下一个bucket的缓冲区，老规矩，之前要将bucket锁住，寻找引用数为0的，且timestamp最小的缓冲区，找到的话就修改缓冲区的同时就该缓冲区移到目标bucket中，期间remove block之后释放该当前bucket的锁，获得目标bucket的id即 `idx=HASH(blockno)`，然后获得目标bucket的锁，并将获得的缓冲区移到目标bucket中。然后将之前的锁释放掉，并获得睡眠锁，并返回。

思考：其实要没有死锁问题，因为remove block的时候一个进程在得到其他bucket的锁的时候已经释放掉当前的bucket锁，一个进程只有一个资源无法构成死锁，破坏了**请求和保持**这个条件。不存在，a进程有bucket1的锁

请求bucket2，而此时bucket2已经被b进程保持着，而b进程此时要请求bucket2.



![](..\pic\bcacheproblem.png)
