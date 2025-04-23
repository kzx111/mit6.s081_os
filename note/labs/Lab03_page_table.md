

# lab3_page_table

# 2021

## speed up system calls

有一些操作系统通过在只读部分分享数据来加速在用户和内核之间某一个系统调用。这会消除当执行每一个系统调用时对内核转换的需要。为了帮助你学习如何在页表中装入映射，你的第一个任务是实现对getid系统调用的优化。

当每一个进程被创建时，在USYSCAL（虚拟地址memlayout.h中）映射一个只读页。在做这个页开始，存储一个struct usyscall (memlayouy.h)并且初始化该结构体来存储目前进程的PID。该实验已经提供ugetid（）在用户空间并ugetid()将自动使用USYSCALL映射。



`pgtbltest.c`

```c
void
ugetpid_test()
{
  int i;

  printf("ugetpid_test starting\n");
  testname = "ugetpid_test";

  for (i = 0; i < 64; i++) {
    int ret = fork();
    if (ret != 0) {
      wait(&ret);
      if (ret != 0)
        exit(1);
      continue;
    }
    if (getpid() != ugetpid())
      err("missmatched PID");
    exit(0);
  }
  printf("ugetpid_test: OK\n");
}
```



其中getpid() != ugetpid()判断当前进程的pid和我们实现的ugetpid是否一致。

memlayout.h中也定义了USYCALLL和该结构体

此时为了每个进程都有usycall属性，则需在proc.h的struct proc中添加struct usyscall结构体

```c
// Per-process state
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  // wait_lock must be held when using this:
  struct proc *parent;         // Parent process

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  struct usyscall *usyscall;   // data page for sharing data between userspace and the kernel
};
```





然后，我们知道pro.c中**proc_pagetable()该函数**可以为用户进程创建一个页表，并将进程的结构映射到该表中。所以需要添加将usyscall属性添加到用户页表中。

```c
// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }


  //map the usyscall page
  if(mappages(pagetable,USYSCALL,PGSIZE,
              (uint64)(p->usyscall),PTE_U|PTE_R)<0){
    uvmfree(pagetable,0);
    return 0;
  }

  return pagetable;
}

```

关于mappage我思考了好久哦，现在隐隐约约明白了

之前我们就明白xv6是三级页表结构，每一个页表只有512项，但是页框有12字节。

mappages调用的walk函数会根据一级页表pagetable来找到三级页表的页表项

之前页可知，物理内存会被操作系统分成很多很多的页，一个数据段或者一段代码段有可能会占用两个页甚至更多，而且数据段和代码段的物理地址未必是页偏移为0的地址，即p->usyscall可能在页框的中间，所以我们需将该物理地址PA2PTE和虚拟地址向下取整分别得到物理页框号和虚拟页表号，walk函数根据虚拟地址页表号找到对应的页表项，并将物理框号对应到页表项中，其中还要通过对va+size-1向下取整来得到该数据段或代码段末尾的虚拟地址的页表号，通过for循环来将连续的物理空间和连续的虚拟地址空间相对应。这里用连续的物理地址说明内核中的数段或代码段是连续的。

注意，permission是PTE_U|PTE_R说明该数据能读和允许用户进程访问







在**allocproc()**函数中，在进程的页表中寻找未使用的进程，如果找到了，就初始化该状态并在内核中运行，并带着锁返回。如果没有空闲进程，或内存分配失败，则返回0.





```c
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }


  //Allocate a usyscall page
  if((p->usyscall=(struct usyscall *)kalloc())==0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  p->usyscall->pid=p->pid;

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }


  //Allocate a usyscall page
  if((p->usyscall=(struct usyscall *)kalloc())==0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  p->usyscall->pid=p->pid;

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }


  //Allocate a usyscall page
  if((p->usyscall=(struct usyscall *)kalloc())==0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  p->usyscall->pid=p->pid;

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}
```

在该函数中初始化分配进程并将该初始化该进程的属性。





freeproc() 则就是释放进程的结构还有数据

```c
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->usyscall)
    kfree((void*)p->usyscall);
  p->usyscall=0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

```

这里我们肯定要释放usyscall这个数据结构。

在该函数中调用了 proc_freepagetable(p->pagetable, p->sz);

```c
// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmunmap(pagetable,USYSCALL,1,0);
  uvmfree(pagetable, sz);
}
```

这个函数就是在pagetable中从va开始的页表项开始逐个释放页表项，并且通过dofree，即dofree为1则释放掉虚拟地址指向的物理地址，为0则不需要释放，一直到npages个页表项都释放完。

```c
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}
```

uvmfree则是虚拟地址0开始释放sz大小，且释放页框，并通过调用freewalk将三级页表全部释放

```c
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}
```

freewalk就非常厉害，通过递归的方式来依次遍历三级页表，一开始遍历第一级页表，然后判断每一个页表项是否被用过且是否不是指向叶子，如果是则指从下一级页表开始，如果遍历到叶子，判断页表项是否备用过，如果是则说明之前没有清除干净，则抱错，panic("freewalk :leaf");并且将该pagetable页表释放掉

```c
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){       //==优先级大于&&
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}
```









## print a page table

题目的意思就是将page table打印出来，第一行是page table 的物理地址

接下来就是用.. 来表示页表的深度，然后打印页表项的内容以及其指向的物理地址（就是从该页表项中提取出来的物理地址） ..0: pte 0x0000000021fda801 pa 0x0000000087f6a000例如这一行就是先将该pte右移10位再左移12位。不能打印无效的ptes。







这个题目真的简单

实现我们上一个实验就分析了freewalk,我们可以根据freewalk来实现这个vmprint函数

```c
void 
vmprint_recursive(pagetable_t p,int level){
  if(level==0)
    printf("page table %p\n",p);
  level++;
  for(int i=0;i<512;i++){
    pte_t pte=p[i];
    if(pte & PTE_V){
      uint64 child = PTE2PA(pte);
      for(int j=0;j<level;j++){
        printf(".. ");
      }
      printf("%d: pte %p pa %p\n",i,pte,child);
      if((pte & (PTE_R|PTE_W|PTE_X)) == 0)
        vmprint_recursive((pagetable_t)child,level);
    }
  }
}

void vmprint(pagetable_t p){
  vmprint_recursive(p,0);
}

```

并在defs定义该函数，

最后就是在exe.c里面的exec()函数中添加  if(p->pid==1) vmprint(p->pagetable);

```c
  p->trapframe->epc = elf.entry;  // initial program counter = main
  p->trapframe->sp = sp; // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);
  if(p->pid==1) vmprint(p->pagetable);

  return argc; // this ends up in a0, the first argument to main(argc, argv)
```

启动qemu我们便可以看到页表了。



## Detecting which pages have been accessed

根据 xv6 手册内容，我们应在 ***kernel/riscv.h*** 内定义 `PTE_A`

```c
// kernel/riscv.h
// ...
#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // 1 -> user can access
#define PTE_A (1L << 6) // 1 -> accessed
//...

```

查阅相关代码发现 `sys_pgaccess()` 相关的入口、函数声明等都已给出，仅定义 `sys_pgaccess()` 即可

```c
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 va;
  int page_nums;
  uint64 user_addr;
  if(argaddr(0,&va)<0)
    return -1;
  if(argint(1,& page_nums)<0)
    return -1;
  if(argaddr(2,&user_addr)<0)
    return -1;


  uint64 mask=0;
  uint64 complement=~PTE_A;
  
  struct proc *p=myproc();
  for(int i=0;i<page_nums;i++){
    pte_t *pte=walk(p->pagetable,va+i*PGSIZE,0);
    if(*pte&PTE_A){
      mask |=(1 << i);
      *pte &= complement;
    }
  }

  copyout(p->pagetable,user_addr,(char*)&mask,sizeof(mask));
  return 0;
}
```

注意， `walk()` 函数未在 ***kernel/def.h*** 中声明，需要在添加这一声明

根据提示，在检查 `PTE_A` 是否被设置之后，需要将其清除（置 0 ）。否则，将无法确定自上次调用 `pgaccess()` 以来页面是否被访问过，换句话说，该位将被永久置位。为将 `PTE_A` 置 0 ，我们将其取反与页表项相与即可。

所以 ` copyout(p->pagetable,user_addr,(char*)&mask,sizeof(mask));` 送往用户空间。

我们在pgtbltest.c中其实就可以发现，

```c
void
pgaccess_test()
{
  char *buf;
  unsigned int abits;
  printf("pgaccess_test starting\n");
  testname = "pgaccess_test";
  buf = malloc(32 * PGSIZE);
  if (pgaccess(buf, 32, &abits) < 0)
    err("pgaccess failed");
  buf[PGSIZE * 1] += 1;
  buf[PGSIZE * 2] += 1;
  buf[PGSIZE * 30] += 1;
  if (pgaccess(buf, 32, &abits) < 0)
    err("pgaccess failed");
  if (abits != ((1 << 1) | (1 << 2) | (1 << 30)))
    err("incorrect access bits set");
  free(buf);
  printf("pgaccess_test: OK\n");
}
```

pgaccess(bur,32,&abits)第一个参数为malloc分配的地址空间的首部地址（且为虚拟地址），位于heap上，分配了32页大小的内存空间。

所以参数二为32，第三个参数为unsigned int ,一共由32位，正好可以满足，每一页用一位来表示。





## Optional challenge exercises

* 使用超级页来减少页表中ptes的数目
* 解除用户进程的第一个页的映射，方便解耦一个空指针将导致错误。你将不得不开始用户文本段在4096而不是0
* 添加一个系统调用来报道脏的页，使用pte_D



# 2020

## Print a page table





## A kernel page table per process



## Simplify copyin/copyinstr





## Optional challenge exercises

* 扩展您的解决方案以支持尽可能大的用户程序；也就是说，要消除用户程序大小必须小于PLIC（平台级中断控制器）区域的限制