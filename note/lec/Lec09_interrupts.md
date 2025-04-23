

# Lec9_interrupts

## 真实操作系统内存使用情况

大部分内存都被使用了，并且RES内存远小于VIRT内存。VIRT表示的是虚拟内存地址空间的大小，RES是实际使用的内存数量。

## Interrupt硬件部分

中断与系统调用主要有3个小的差别：

* asynchronous(异步)。当硬件生成中断时，Interrupt handler与当前运行的进程在CPU上没有任何关联。但如果是系统调用的话，系统调用发生在运行进程的context下。
* concurrency(并发)。对于中断来说，CPU和生成中断的设备是并行的在运行。网卡自己独立的处理来自网络的packet，然后在某个时间点产生中断，但是同时，CPU也在运行。所以我们在CPU和设备之间是真正的并行的，我们必须管理这里的并行。
* program device(设备编程)。我们这节课主要关注外部设备，例如网卡，UART，而这些设备需要被编程。每个设备都有一个编程手册，就像RISC-V有一个包含了指令和寄存器的手册一样。设备的编程手册包含了它有什么样的寄存器，它能执行什么样的操作，在读写控制寄存器的时候，设备会如何响应。不过通常来说，设备的手册不如RISC-V的手册清晰，这会使得对于设备的编程会更加复杂。



## 设备驱动概述

​	管理设备的代码称为驱动，所有的驱动都在内核中。我们今天要看的是UART设备的驱动，代码在uart.c文件中。如果我们查看代码的结构，我们可以发现大部分驱动都分为两个部分，bottom/top。bottom部分通常是Interrupt handler。当一个中断送到了CPU，并且CPU设置接收这个中断，CPU会调用相应的Interrupt handler。Interrupt handler并不运行在任何特定进程的context中，它只是处理中断。top部分，是用户进程，或者内核的其他部分调用的接口。对于UART来说，这里有read/write接口，这些接口可以被更高层级的代码调用。通常情况下，驱动中会有一些队列（或者说buffer），top部分的代码会从队列中读写数据，而Interrupt handler（bottom部分）同时也会向队列中读写数据。这里的队列可以将并行运行的设备和CPU解耦开来。

![](..\pic\image (4).png)

​		如何对设备进行编程？

​		编程是通过**memory mapped I/O**完成的。在SiFive的手册中，设备地址出现在物理地址的特定区间内，这个区间由主板制造商决定。操作系统需要知道这些设备位于物理地址空间的具体位置，然后再通过普通的load/store指令对这些地址进行编程。load/store指令实际上的工作就是读写设备的控制寄存器。

## 在XV6中设置中断

​		对于“$ ”来说，实际上就是设备会将字符传输给UART的寄存器，UART之后会在发送完字符之后产生一个中断。在QEMU中，模拟的线路的另一端会有另一个UART芯片（模拟的），这个UART芯片连接到了虚拟的Console，它会进一步将“$ ”显示在console上。

​		对于“ls”，这是用户输入的字符。键盘连接到了UART的输入线路，当你在键盘上按下一个按键，UART芯片会将按键字符通过串口线发送到另一端的UART芯片。另一端的UART芯片先将数据bit合并成一个Byte，之后再产生一个中断，并告诉处理器说这里有一个来自于键盘的字符。之后Interrupt handler会处理来自于UART的字符。

​		RISC-V有许多与中断相关的寄存器：

- SIE（Supervisor Interrupt Enable）寄存器。这个寄存器中有一个bit（E）专门针对例如UART的外部设备的中断；有一个bit（S）专门针对软件中断，软件中断可能由一个CPU核触发给另一个CPU核；还有一个bit（T）专门针对定时器中断。我们这节课只关注外部设备的中断。
- SSTATUS（Supervisor Status）寄存器。这个寄存器中有一个bit来打开或者关闭中断。每一个CPU核都有独立的SIE和SSTATUS寄存器，除了通过SIE寄存器来单独控制特定的中断，还可以通过SSTATUS寄存器中的一个bit来控制所有的中断。
- SIP（Supervisor Interrupt Pending）寄存器。当发生中断时，处理器可以通过查看这个寄存器知道当前是什么类型的中断。
- SCAUSE寄存器，这个寄存器我们之前看过很多次。它会表明当前状态的原因是中断。
- STVEC寄存器，它会保存当trap，page fault或者中断发生时，CPU运行的用户程序的程序计数器，这样才能在稍后恢复程序的运行。



uartinit()函数：

先关闭中断，之后设置波特率，设置字符长度为8bit，重置FIFO，最后再重新打开中断。



## UART驱动的top部分

如何从Shell程序输出提示符“$ ”到Console

<img src="..\pic\uartdriver_top.JPG" style="zoom: 200%;" />





##  UART驱动的bottom部分

![](..\pic\uart_buttom.JPG)





##  UART读取键盘输入



在UART的另一侧，会有类似的事情发生，有时Shell会调用read从键盘中读取字符。 在read系统调用的底层，会调用fileread函数。

![](..\pic\keyboardintr.JPG)





Q:

学生提问：uartinit只被调用了一次，所以才导致了所有的CPU核都共用一个buffer吗？

Frans教授：因为只有一个UART设备，一个buffer只针对一个UART设备，而这个buffer会被所有的CPU核共享，这样运行在多个CPU核上的多个程序可以同时向Console打印输出，而驱动中是通过锁来确保多个CPU核上的程序串行的向Console打印输出。

学生提问：我们之所以需要锁是因为有多个CPU核，但是却只有一个Console，对吧？

Frans教授：是的，如我们之前说的驱动的top和bottom部分可以并行的运行。所以一个CPU核可以执行uartputc函数，而另个一CPU核可以执行uartintr函数，我们需要确保它们是串行执行的，而锁确保了这一点。

学生提问：那是不是意味着，某个时间，其他所有的CPU核都需要等待某一个CPU核的处理？

Frans教授：这里并不是死锁。其他的CPU核还是可以在等待的时候运行别的进程。

