



# Lec08 Page faults (Frans)

# Page Fault Basics

​	1. 你可以认为虚拟内存有两个主要的优点：

​		第一个是Isolation，隔离性。虚拟内存使得操作系统可以为每个应用程序提供属于它们自己的地址空间。所以一个应用程序不可能有意或者无意的修改另一个应用程序的内存数据。虚拟内存同时也提供了用户空间和内核空间的隔离性，我们在之前的课程已经谈过很多相关内容，并且你们通过page table lab也可以理解虚拟内存的隔离性。

​		另一个好处是level of indirection，提供了一层抽象。处理器和所有的指令都可以使用虚拟地址，而内核会定义从虚拟地址到物理地址的映射关系。这一层抽象是我们这节课要讨论的许多有趣功能的基础。不过到目前为止，在XV6中内存地址的映射都比较无聊，实际上在内核中基本上是直接映射（注，也就是虚拟地址等于物理地址）。当然也有几个比较有意思的地方：

* trampoline page，它使得内核可以将一个物理内存page映射到多个用户地址空间中。

- guard page，它同时在内核空间和用户空间用来保护Stack。

​		而之前所讲的内存映射相对来说比较静态，（在最开始的时候设置好，之后就不会再做任何变动），而page fault 就柯让这种地址映射关系变得动起来。



2. 从硬件和XV6的角度来说，当出现了page fault，现在有了3个对我们来说极其有价值的信息，分别是：
   - the **faulting va**(stval)出错的虚拟地址，即触发page fault 的源。出现page fault xv6会将出错的虚拟地址保存在stval寄存器中。
   - **the type** of page fault (scause)
   - **the va of instruction** that caused the fault (sepc)page fault在用户空间发生的位置





#  Lazy page allocation



  1. ​		**sbrk()系统调用**：可以让用户程序能扩大自己的heap。当一个应用程序启动的时候，sbrk指向的是heap的最底端，同时也是stack的最顶端。这个位置通过代表进程的数据结构中的sz字段表示，这里以*p->sz表示*。当调用sbrk时，sbrk会扩展heap的上边界（也就是会扩大heap）。默认eager allocation，一旦调用了sbrk，内核会立即分配应用程序所需要的物理内存。但是实际上，对于应用程序来说很难预测自己需要多少内存，所以通常来说，应用程序倾向于申请多于自己所需要的内存。这意味着，进程的内存消耗会增加许多，但是有部分内存永远也不会被应用程序所使用到。

     ​		使用虚拟内存和page fault handler，我们完全可以用某种更聪明的方法来解决这里的问题，这里就是利用**lazy allocation**。sbrk系统调基本上不做任何事情，唯一需要做的事情就是提升*p->sz*，将*p->sz*增加n，其中n是需要新分配的内存page数量。但是内核在这个时间点并不会分配任何物理内存。之后在某个时间点，应用程序使用到了新申请的那部分内存，这时会触发page fault，因为我们还没有将新的内存映射到page table。所以，如果我们解析一个大于旧的*p->sz*，但是又小于新的*p->sz（注，也就是旧的p->sz + n）*的虚拟地址（因为如果x虚拟地址大于p->sz+n那么就是一个无效地址，对应的就是一个程序错误，这意味着用户应用程序在尝试解析一个自己不拥有的内存地址），我们希望内核能够分配一个内存page，并且重新执行指令。

例如，

```c
uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  myproc()->sz=myproc()->sz+n;
  // if(growproc(n) < 0)
  //   return -1;
  return addr;
}
```

让sbrk系统调用只是增加进程大小即myproc()->sz=myproc()->sz+n;

修改完之后启动XV6，并且执行“echo hi”，我们会得到一个page fault。

![](C:\Users\Jack\Desktop\os note\pic\image.png)

根据sepc我们可以找到发生缺页异常的用户空间的虚拟地址(在sh.asm里找)

![](C:\Users\Jack\Desktop\os note\pic\image (1).png)

如果我们向前看看汇编代码，我们可以看到page fault是出现在malloc的实现代码中。这也非常合理，在malloc的实现中，我们使用sbrk系统调用来获得一些内存，之后会初始化我们刚刚获取到的内存，在0x12a4位置，刚刚获取的内存中写入数据，但是实际上我们在向未被分配的内存写入数据。另一个可以证明内存还没有分配的地方是，XV6中Shell通常是有4个page，包含了text和data。出错的地址在4个page之外，也就是第5个page，实际上我们在4个page之外8个字节。这也合理，因为在0x12a4对应的指令中，a0持有的是0x4000，而8相对a0的偏移量。偏移之后的地址就是我们想要使用的地址（注，也就是出错的地址）。

然后将usertrap函数改为

```c
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else if(r_scause()==15){
    uint64 va=r_stval();
    printf("page fault %p\n",va);
    uint64 ka=(uint64)kalloc();
    if(ka==0){
      p->killed =1;
    }else{
      memset((void *)ka,0,PGSIZE);
      va=PGROUNDDOWN(va);
      if(mappages(p->pagetable,va,PGSIZE,ka,PTE_W|PTE_U|PTE_R)!=0){
        kfree((void*)ka);
        p->killed=1;
      }
    }
  }else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }


  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

```

添加缺页处理。

然后，重新运行

![](C:\Users\Jack\Desktop\os note\pic\image (2).png)



这里有异常说明，uvmunmap释放用户映射的时候发现要释放的页表项并未分配物理地址，所以将其中代码改为

```c
 if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      continue;
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
```

然后运行就可以正常运行，这就是lazy allication

![](C:\Users\Jack\Desktop\os note\pic\image (3).png)



Q我并不能理解为什么在uvmunmap中可以直接改成continue？

之前的panic表明，我们尝试在释放一个并没有map的page。怎么会发生这种情况呢？唯一的原因是sbrk增加了p->sz，但是应用程序还没有使用那部分内存。因为对应的物理内存还没有分配，所以这部分新增加的内存的确没有映射关系。我们现在是lazy allocation，我们只会为需要的内存分配物理内存page。如果我们不需要这部分内存，那么就不会存在map关系，这非常的合理。相应的，我们对于这部分内存也不能释放，因为没有实际的物理内存可以释放，所以这里最好的处理方式就是continue，跳过并处理下一个page。

Q在uvmunmap中，我认为之前的panic存在是有理由的，我们是不是应该判断一下，然后对于特定的场景还是panic？

为什么之前的panic会存在？对于未修改的XV6，永远也不会出现用户内存未map的情况，所以一旦出现这种情况需要panic。但是现在我们更改了XV6，所以我们需要去掉这里的panic，因为之前的不可能变成了可能。





# Zero Fill On Demand

​		教授这一块首先讲，一个用户程序的地址空间，从低地址到高地址分别是text ,   data, 还有bbs区（BSS区域包含了未被初始化或者初始化为0的全局或者静态变量）。当编译器在生成二进制文件时，编译器会填入这三个区域。text区域是程序的指令，data区域存放的是初始化了的全局变量，BSS包含了未被初始化或者初始化为0的全局变量。

​		这里我们可以优化，BSS里都为0，所以我们只需要将bss全部映射到同一个全部未0的物理页。但是我们不能对这页进行写操作，只能读。之后我们尝试对BSS写操作时，就会有page fault。此时就是在物理内存申请一个新的内存page，将其内容设置为0。之后更新这个page的mapping关系，将该pte设置为可读可写，并指向新的物理页，然后重新执行指令。虽然，在exec中需要做的工作变少了。程序可以启动的更快，这样你可以获得更好的交互体验，因为你只需要分配一个内容全是0的物理page。所有的虚拟page都可以映射到这一个物理page上，提升节省了空间。但是，因为每次都会触发一个page fault，update和write会变得更慢吧





# Copy On Write Fork

​		下一个是一个非常常见的优化，许多操作系统都实现了它，同时它也是后面一个实验的内容。这就是copy-on-write fork，有时也称为COW fork。

​		当我们创建子进程时，与其创建，分配并拷贝内容到新的物理内存，其实我们可以直接共享父进程的物理内存page。所以这里，我们可以设置子进程的PTE指向父进程对应的物理内存page。因为一旦子进程想要修改这些内存的内容，相应的更新应该对父进程不可见，因为我们希望在父进程和子进程之间有强隔离性，所以这里我们需要更加小心一些。为了确保进程间的隔离性，我们可以将这里的父进程和子进程的PTE的标志位都设置成只读的。

​		当我们需要更改内存的内容时，我们会得到page fault，因为现在在向一个只读的PTE写数据。假设现在是子进程在执行store指令，那么我们会分配一个新的物理内存page，然后将page fault相关的物理内存page拷贝到新分配的物理内存page中，并将新分配的物理内存page映射到子进程。这时，新分配的物理内存page只对子进程的地址空间可见，所以我们可以将相应的PTE设置成可读写，并且我们可以重新执行store指令。实际上，对于触发刚刚page fault的物理page，因为现在只对父进程可见，相应的PTE对于父进程也变成可读写的了。

Q	当发生page fault时，我们其实是在向一个只读的地址执行写操作。内核如何能分辨现在是一个copy-on-write fork的场景，而不是应用程序在向一个正常的只读地址写数据。是不是说默认情况下，用户程序的PTE都是可读写的，除非在copy-on-write fork的场景下才可能出现只读的PTE？



内核必须要能够识别这是一个copy-on-write场景。几乎所有的page table硬件都支持了这一点。我们之前并没有提到相关的内容，下图是一个常见的多级page table。对于PTE的标志位，我之前介绍过第0bit到第7bit，但是没有介绍最后两位RSW。这两位保留给supervisor software使用，supervisor softeware指的就是内核。内核可以随意使用这两个bit位。所以可以做的一件事情就是，将bit8标识为当前是一个copy-on-write page。当内核在管理这些page table时，对于copy-on-write相关的page，内核可以设置相应的bit位，这样当发生page fault时，我们可以发现如果copy-on-write bit位设置了，我们就可以执行相应的操作了。否则的话，比如说lazy allocation，我们就做一些其他的处理操作。

在copy-on-write lab中，你们会使用RSW在PTE中设置一个copy-on-write标志位。









# Demand Paging

​		这里主要讲按需分配，例如，应用程序是从地址0开始运行。text区域从地址0开始向上增长。位于地址0的指令是会触发第一个page fault的指令，因为我们还没有真正的加载内存。此时，我们需要在某个地方记录了这些page对应的程序文件，我们在page fault handler中需要从程序文件中读取page数据，加载到内存中；之后将内存page映射到page table；最后再重新执行指令。

​		在lazy allocation中，如果内存耗尽了该如何办？如果内存耗尽了，一个选择是撤回page（evict page）。比如说将部分内存page中的内容写回到文件系统再撤回page。一旦你撤回并释放了page，那么你就有了一个新的空闲的page，你可以使用这个刚刚空闲出来的page，分配给刚刚的page fault handler，再重新执行指令。

​		例如Least Recently Used算法，考研学了无数遍了。

​		除了这个策略之外，还有一些其他的小优化。如果你要撤回一个page，你需要在dirty page和non-dirty page中做选择。dirty page是曾经被写过的page，而non-dirty page是只被读过，但是没有被写过的page。你们会选择哪个来撤回，当然是现实中会选择**non-dirty page**



# Memory Mapped Files

​		这里的核心思想是，将完整或者部分文件加载到内存中，这样就可以通过内存地址相关的load或者store指令来操纵文件。为了支持这个功能，一个现代的操作系统会提供一个叫做mmap的系统调用。

​		在任何聪明的内存管理机制中，所有的这些都是以lazy的方式实现。你不会立即将文件内容拷贝到内存中，而是先记录一下这个PTE属于这个文件描述符。相应的信息通常在VMA结构体中保存，VMA全称是Virtual Memory Area。例如对于这里的文件f，会有一个VMA，在VMA中我们会记录文件描述符，偏移量等等，这些信息用来表示对应的内存虚拟地址的实际内容在哪，这样当我们得到一个位于VMA地址范围的page fault时，内核可以从磁盘中读数据，并加载到内存中。所以这里回答之前一个问题，dirty bit是很重要的，因为在unmap中，你需要向文件回写dirty block。

