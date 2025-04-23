# file system

## 关于log的设计



![](..\pic\log.png)



* commit 函数，from就是更新过的数据块，to就是磁盘log区域在内存缓冲区对应的数据块

该函数先将from数据块复制到to数据块内存中，将to数据块写到磁盘log区

* 为什么这种log机制可以实现事务的原子性呢

```c
void
begin_op(void)
{
  acquire(&log.lock);
  while(1){
    if(log.committing){
      sleep(&log, &log.lock);
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
      // this op might exhaust log space; wait for commit.
      sleep(&log, &log.lock);
    } else {
      log.outstanding += 1;
      release(&log.lock);
      break;
    }
  }
}
```





这里关键是 log.outstanding +=1;等带其他fs系统调用结束

```c
// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){
    do_commit = 1;
    log.committing = 1;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
  }
  release(&log.lock);

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}
```

z这里的函数就非常关键，首先，关键就是commit函数，

```c

static void
commit()
{
  if (log.lh.n > 0) {
    write_log();     // Write modified blocks from cache to log
    write_head();    // Write header to disk -- the real commit
    install_trans(0); // Now install writes to home locations
    log.lh.n = 0;
    write_head();    // Erase the transaction from the log
  }
}

```

commit 函数会调用write_log():将已经修改的数据块放到磁盘log区

然后就是write_head():就是把log头部写到磁盘log区

如果在这个函数之前crash，并不会发生什么事，因为该函数未执行，log区头部就只是空的，相当于log没有记录，虽然记录了一下修改过的数据块，但是没有head来控制。

如果这个函数之后crash，那就相当于更新了，因为系统初始化的时候会根据log区将log区的数据还原到原先应该的位置

```c
// Copy committed blocks from log to their home location
static void
install_trans(int recovering)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *lbuf = bread(log.dev, log.start+tail+1); // read log block
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst
    memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst
    bwrite(dbuf);  // write dst to disk
    if(recovering == 0)
      bunpin(dbuf);
    brelse(lbuf);
    brelse(dbuf);
  }
}

static void
recover_from_log(void)
{
  read_head();
  install_trans(1); // if committed, copy from log to disk
  log.lh.n = 0;
  write_head(); // clear the log
}
```



* log_write可以代替bwrite,是因为cache eviction（假设transaction还在进行中，我们刚刚更新了block 45，正要更新下一个block，而整个buffer cache都满了并且决定撤回block 45。在buffer cache中撤回block 45意味着我们需要将其写入到磁盘的block 45位置，这里会不会有问题？如果我们这么做了的话，会破坏什么规则吗？是的，如果将block 45写入到磁盘之后发生了crash，就会破坏transaction的原子性。这里也破坏了前面说过的write ahead rule，write ahead rule的含义是，你需要先将所有的block写入到log中，之后才能实际的更新文件系统block。所以buffer cache不能撤回任何还位于log的block。）

而这个函数有bpin，将block固定在buffer cache中。它是通过给block cache增加引用计数来避免cache撤回对应的block。在之前（注，详见14.6）我们看过，如果引用计数不为0，那么buffer cache是不会撤回block cache的（brelse函数中）。相应的在将来的某个时间，所有的数据都写入到了log中，我们可以在cache中unpin block（注，在15.5中的install_trans函数中会有unpin，因为这时block已经写入到了log中）。

```c
void
log_write(struct buf *b)
{
  int i;

  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  acquire(&log.lock);
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // log absorbtion
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n) {  // Add new block to log?
    bpin(b);
    log.lh.n++;
  }
  release(&log.lock);
}
```

* 为什么XV6的log大小是30个block？因为30比任何一个文件系统操作涉及的写操作数都大，Robert和我看了一下所有的文件系统操作，发现都远小于30，所以就将XV6的log大小设为30。



* **begin_op()**首先，如果log正在commit过程中，那么就等到log提交完成，因为我们不能在install log的过程中写log；其次，如果当前操作是允许并发的操作个数的后一个，那么当前操作可能会超过log区域的大小，我们也需要sleep并等待所有之前的操作结束；最后，如果当前操作可以继续执行，需要将log的outstanding字段加1，最后再退出函数并执行文件系统操作。



* **end_op()**  在最开始首先会对log的outstanding字段减1，因为一个transaction正在结束；其次检查committing状态，当前不可能在committing状态，所以如果是的话会触发panic；如果当前操作是整个并发操作的最后一个的话（log.outstanding == 0），接下来立刻就会执行commit；如果当前操作不是整个并发操作的最后一个的话，我们需要唤醒在begin_op中sleep的操作，让它们检查是不是能运行。

  （注，这里的outstanding有点迷，它表示的是当前正在并发执行的文件系统操作的个数，MAXOPBLOCKS定义了一个操作最大可能涉及的block数量。在begin_op中，只要log空间还足够，就可以一直增加并发执行的文件系统操作。所以XV6是通过设定了MAXOPBLOCKS，再间接的限定支持的并发文件系统操作的个数）







* File system challenges

1. cache eviction
2. 文件系统操作必须适配log的大小。
3. 并发文件系统调用











* 最后我希望同学们记住的有关logging和ext3的是：
  - log是为了保证多个步骤的写磁盘操作具备原子性。在发生crash时，要么这些写操作都发生，要么都不发生。这是logging的主要作用。
  - logging的正确性由write ahead rule来保证。你们将会在故障恢复相关的业务中经常看到write ahead rule或者write ahead log（WAL）。write ahead rule的意思是，你必须在做任何实际修改之前，将所有的更新commit到log中。在稍后的恢复过程中完全依赖write ahead rule。对于文件系统来说，logging的意义在于简单的快速恢复。log中可能包含了数百个block，你可以在一秒中之内重新执行这数百个block，不管你的文件系统有多大，之后又能正常使用了。
  - 最后有关ext3的一个细节点是，它使用了批量执行和并发来获得可观的性能提升，不过同时也带来了可观的复杂性的提升。

