

# Lec06 Isolation & system call entry/exit (Robert)





我们接下来看看supervisor mode可以控制什么？

现在可以读写控制寄存器了。另一件事情supervisor mode可以做的是，它可以使用PTE_U标志位为0的PTE。



Q:难道vm.c里的函数不是要直接访问物理内存吗？

是的，这些函数能这么做的原因是，内核小心的在page table中设置好了各个PTE。这样当内核收到了一个读写虚拟内存地址的请求，会通过kernel page table将这个虚拟内存地址翻译成与之等价物理内存地址，再完成读写。所以，一旦使用了kernel page table，就可以非常方便的在内核中使用所有这些直接的映射关系。但是直到trap机制切换到内核之前，这些映射关系都不可用。直到trap机制将程序运行切换到内核空间之前，我们使用的仍然是没有这些方便映射关系的user page table。



Q:read和write系统调用，相比内存的读写，他们的代价都高的多，因为它们需要切换模式，并来回捣腾。有没有可能当你执行打开一个文件的系统调用时， 直接得到一个page table映射，而不是返回一个文件描述符？这样只需要向对应于设备的特定的地址写数据，程序就能通过page table访问特定的设备。你可以设置好限制，就像文件描述符只允许修改特定文件一样，这样就不用像系统调用一样在用户空间和内核空间来回捣腾了。



这是个很好的想法。实际上很多操作系统都提供这种叫做内存映射文件（Memory-mapped file access）的机制，在这个机制里面通过page table，可以将用户空间的虚拟地址空间，对应到文件内容，这样你就可以通过内存地址直接读写文件。实际上，你们将在mmap 实验中完成这个机制。对于许多程序来说，这个机制的确会比直接调用read/write系统调用要快的多。





Q:为什么我们在gdb中看不到ecall的具体内容？或许我错过了，但是我觉得我们是直接跳到trampoline代码的。



ecall只会更新CPU中的mode标志位为supervisor，并且设置程序计数器成STVEC寄存器内的值。在进入到用户空间之前，内核会将trampoline page的地址存在STVEC寄存器中。所以ecall的下一条指令的位置是STVEC指向的地址，也就是trampoline page的起始地址。（注，实际上ecall是CPU的指令，自然在gdb中看不到具体内容）







**pc指令寄存器里面存放虚拟地址，和其他所有指令使用的地址一样**

**ecall 只会改变三件事情**：

第一，ecall将代码从user mode 改到supervisor mode

第二，ecall将程序计数器的值保存在SEPC寄存器。且pc值变为stvec寄存器的值（里面是trampoline页的起始值）

第三，ecall将跳转指向stvec的值。





将a0和sscratch交换是因为指令之前sscratch里存了trapframe的虚拟地址，而a0则是系统调用的第一个参数。







It writes the physical address of the root page-table page into the register satp. 





















