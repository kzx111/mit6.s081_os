# Lab 7: Multithreading

每一个进程一个内核栈。

未解之谜：进程的内核栈和用户栈位置不一样吗？





### 第一个实验就是让我们根据内核线程的机制来构建用户级线程机制。

![](..\pic\pro.png)

根据原先的proc和之前内核线程调度所用的变量，我们可以类比得，一个线程需要线程私有的栈，线程的状态，以及线程的上下文





所以第一步就是定义上下文结构体

第二部就是创建线程的时候对线程初始化

```c
void 
thread_create(void (*func)())
{
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->state = RUNNABLE;
  // YOUR CODE HERE
  // set thread's function address and thread's stack pointer - lab7-1
  t->context.ra=(uint64) func;
  t->context.sp=(uint64) t->stack + STACK_SIZE;
}
```

第三步就是调度了，调度函数我们模仿内核调度函数来写

```c
void 
thread_schedule(void)
{
  struct thread *t, *next_thread;

  /* Find another runnable thread. */
  next_thread = 0;
  t = current_thread + 1;
  for(int i = 0; i < MAX_THREAD; i++){
    if(t >= all_thread + MAX_THREAD)
      t = all_thread;
    if(t->state == RUNNABLE) {
      next_thread = t;
      break;
    }
    t = t + 1;
  }

  if (next_thread == 0) {
    printf("thread_schedule: no runnable threads\n");
    exit(-1);
  }

  if (current_thread != next_thread) {         /* switch threads?  */
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;
    /* YOUR CODE HERE
     * Invoke thread_switch to switch from t to next_thread:
     * thread_switch(??, ??);
     */
    thread_switch((uint64)&t->context,(uint64) &current_thread->context);   // switch thread - lab7-1
  } else
    next_thread = 0;
}
```



而 thread_switch()这个函数和内核的swtch.s一模一样。





Q: thread_switch needs to save/restore only the callee-save registers. Why?
A: 这里仅需保存被调用者保存(callee-save)寄存器的原因和 xv6 中内核线程切换时仅保留 callee-save 寄存器的原因是相同的. 由于 thread_switch() 一定由其所在的 C 语言函数调用, 因此函数的调用规则是满足 xv6 的函数调用规则的, 对于其它 caller-save 寄存器都会被保存在线程的堆栈上, 在切换后的线程上下文恢复时可以直接从切换后线程的堆栈上恢复 caller-save 寄存器的值. 由于 callee-save 寄存器是由被调用函数即 thread_switch() 进行保存的, 在函数返回时已经丢失, 因此需要额外保存这些寄存器的内容







### 第二个实验Using threads

这个任务，我们将使用哈希表探索使用线程和锁的并发编程。

文件notxv6/ph.c包含一个简单的哈希表，如果被一个单个的线程使用的话，那么该哈希表是正确的。如果被使用在多线程则不正确。

使用如下命令构建 `ph` 程序, 该程序包含一个线程不安全的哈希表.

```
$ make ph
```

运行 `./ph 1` 即使用单线程运行该哈希表, 输出如下, 其 0 个键丢失:

![在这里插入图片描述](https://i-blog.csdnimg.cn/blog_migrate/a6b8b209ffa57620efc7df235a8b9e8c.png)

运行 `./ph 2` 即使用两个线程运行该哈希表, 输出如下, 可以看到其 put 速度近乎先前 2 倍, 但是有 16423 个键丢失, 也说明了该哈希表非线程安全.

![在这里插入图片描述](https://i-blog.csdnimg.cn/blog_migrate/b1b98836f55f2abd5132efdbae0234bb.png)









Q: Why are there missing keys with 2 threads, but not with 1 thread? Identify a sequence of events with 2 threads that can lead to a key being missing. Submit your sequence with a short explanation in answers-thread.txt

因为两个线程并发put和get同一个不安全的哈希表，

**解释要结合notxv6/ph.c来分析，**

这里的哈希表就是"数组(bucket)+链表"的经典实现方法. 通过取余确定 bucket, put() 是使用前插法插入键值对, get() 遍历 bucket 下的链表找到对应 key 的 entry. 而这个实现没有涉及任何锁机制或者 CAS 等线程安全机制, 因此线程不安全, 多线程插入时会出现数据丢失.
该哈希表的线程安全问题是: 多个线程同时调用 put() 对同一个 bucket 进行数据插入时, 可能会使得先插入的 entry 丢失. 具体来讲, 假设有 A 和 B 两个线程同时 put(), 而恰好 put() 的参数 key 对应到了哈希表的同一 bucket. 同时假设 A 和 B 都运行到 put() 函数的 insert() 处, 还未进入该函数内部, 这就会导致两个线程 insert() 的后两个参数是相同的, 都是当前 bucket 的链表头, 如若线程 A 调用 insert() 插入完 entry 后, 切换到线程 B 再调用 insert() 插入 entry, 则会导致线程 A 刚刚插入的 entry 丢失.





步骤：
定义互斥锁数组--->在 `main()` 函数中对所有互斥锁进行初始化.---->在 `put()` 中加锁.——>不需要在 `get()` 中加锁

——>增大 `NBUCKET`.（因此, 哈希冲突是完全随机的. 而由于 NBUCKET 较小, 因此产生冲突的概率相对较大.
而修改 NBUCKET 为 7 实际上是增大了 bucket 数, 从而减少了冲突的概率, 因此最后性能仍会有一定的提升. (感谢多位读者提出问题, 特别感谢 “m0_50699802” 朋友给笔者指明了问题错误所在.)  ）

### lab3 Barrier

分析barrier .c文件

```c
static void *
thread(void *xa)
{
  long n = (long) xa;
  long delay;
  int i;

  for (i = 0; i < 20000; i++) {   //一共有20000个回合
    int t = bstate.round;
    assert (i == t);    //如果i!=t那么就会fail
    barrier();
    usleep(random() % 100);
  }

  return 0;
}
```

模拟该函数线程的并发问题，一共20000趟数，

```c
struct barrier {
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread;      // Number of threads that have reached this round of the barrier
  int round;     // Barrier round
} bstate;
```

barrier .round 来表示趟数，每一趟要拦住所有的线程。

利用好一下两函数

```c
pthread_cond_wait(&cond, &mutex);  // go to sleep on cond, releasing lock mutex, acquiring upon wake up
pthread_cond_broadcast(&cond);     // wake up every thread sleeping on cond
```

```c
static void 
barrier()
{
  // YOUR CODE HERE
  //
  // Block until all threads have called barrier() and
  // then increment bstate.round.
  //
  pthread_mutex_lock(&bstate.barrier_mutex);
  bstate.nthread+=1;
  if(bstate.nthread==nthread){
    pthread_cond_broadcast(&bstate.barrier_cond);
    bstate.nthread=0;
    bstate.round+=1;
  }else{
    pthread_cond_wait(&bstate.barrier_cond,&bstate.barrier_mutex);
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);
  
}
```

之所以要加锁，是因为bstate 是公告资源，并且线程要对其进行改变。

