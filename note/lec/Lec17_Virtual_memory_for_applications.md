#  Lec 17: Virtual memory for applications 

[论文](https://pdos.csail.mit.edu/6.828/2020/readings/appel-li.pdf)：看不懂，浅浅读了一下

kernel可以通过虚拟内存来获得很多特性，比如copy-on-write、lazy allocation等，user program也可以通过虚拟内存获得一些特性。我们这里说的虚拟内存是指：User Mode或者应用程序想要使用与内核相同的机制，来产生Page Fault并响应Page Fault

比如：

1. Concurrent garbage collector
2. shared virtual memory
3. Concurrent check-pointing
4. Generational garbage collector
5. Persistent stores
6. Extending addressability
7. Data-compression paging
8. Heap overflow detection

简单了解一下，我是真的看不懂，等以后深入操作系统，可以试一试研究一下这些知识。



> 这里关于kernel调用虚拟内存获得copy-on-write的理解是，当子进程写数据，发生pagefault的时候，kernel在内核虚拟地址空间中，申请一个内存块，并让子进程的虚拟地址指向该内存块，然后重新执行发生pagefault的指令
>
> user program可以使用虚拟地址，应该指的是，



**user-level VM primitive **应用程序使用虚拟内存所需要特性

trap: 以user mode进入page fault trap并进行处理

prot: 降低页的访问权限

e.g. `mprotect(addr, len, PROT_READ)`：让被映射的地址addr只能有读权限

unprot: 提高页的访问权限

dirty: 返回一个自上一个调用以来已经被使用了的page

map2: 将同一个physical page map到2个不同的virtual address，设置不同的访问权限







# 支持应用程序使用虚拟内存的系统调用

mmap(),mprotext(),mumap(),sigaction().



# 虚拟内存系统如何支持用户应用程序

​		第一个是虚拟内存系统为了支持这里的特性，具体会发生什么？通常来说，地址空间还包含了一些操作系统的数据结构，这些数据结构与任何硬件设计都无关，它们被称为Virtual Memory Areas（VMAs）。VMA会记录一些有关连续虚拟内存地址段的信息。在一个地址空间中，可能包含了多个section，每一个section都由一个连续的地址段构成，对于每个section，都有一个VMA对象。连续地址段中的所有Page都有相同的权限，并且都对应同一个对象VMA（例如一个进程的代码是一个section，数据是另一个section，它们对应不同的VMA，VMA还可以表示属于进程的映射关系，例如下面提到的Memory Mapped File）。

​		举个例子，如果进程有一个Memory Mapped File，那么对于这段地址，会有一个VMA与之对应，VMA中会包含文件的权限，以及文件本身的信息，例如文件描述符，文件的offset等。在接下来的mmap lab中，你们将会实现一个非常简单版本的VMA，并用它来实现针对文件的mmap系统调用。你可以在VMA中记录mmap系统调用参数中的文件描述符和offset。

​		第二个部分,我们假设一个PTE被标记成invalid或者只读，而你想要向它写入数据。这时，CPU会跳转到kernel中的固定程序地址，也就是XV6中的trampoline代码（注，详见6.2）。kernel会保存应用程序的状态，在XV6中是保存到trapframe。之后再向虚拟内存系统查询，现在该做什么呢？虚拟内存系统或许会做点什么，例如在lazy lab和copy-on-write lab中，trap handler会查看Page Table数据结构。而在我们的例子中会查看VMA，并查看需要做什么。举个例子，如果是segfault，并且应用程序设置了一个handler来处理它，那么

- segfault事件会被传播到用户空间
- 并且通过一个到用户空间的upcall在用户空间运行handler
- 在handler中或许会调用mprotect来修改PTE的权限
- 之后handler返回到内核代码
- 最后，内核再恢复之前被中断的进程。