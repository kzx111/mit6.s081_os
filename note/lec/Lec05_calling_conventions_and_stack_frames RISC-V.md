# calling conventions and stack frames RISC-V



![](C:\Users\Jack\Desktop\os note\pic\callee_caller.png)



[具体内容材料](https://pdos.csail.mit.edu/6.828/2020/readings/riscv-calling.pdf)

我发现最简单的记住它们的方法是：

- Caller Saved寄存器在函数调用的时候不会保存
- Callee Saved寄存器在函数调用的时候会保存

​	一个Caller Saved寄存器可能被其他函数重写。假设我们在函数a中调用函数b，任何被函数a使用的并且是Caller Saved寄存器，调用函数b可能重写这些寄存器。我认为一个比较好的例子就是Return address寄存器（注，保存的是函数返回的地址），你可以看到ra寄存器是Caller Saved，这一点很重要，它导致了当函数a调用函数b的时侯，b会重写Return address。所以基本上来说，任何一个Caller Saved寄存器，作为调用方的函数要小心可能的数据可能的变化；任何一个Callee Saved寄存器，作为被调用方的函数要小心寄存器的值不会相应的变化。我经常会弄混这两者的区别，然后会到这张表来回顾它们

我个人观点就是，虽然caller saved 寄存器在函数调用的时候不会保存，但是他会保存在caller函数的栈中。



如果要在过程调用后恢复该值，则调用方有责任将这些寄存器压入堆栈或复制到其他位置，而callee保存的寄存器会被保存，称为非易失性寄存器，可以期望这些寄存器在被调用者返回后保持相同的值。

