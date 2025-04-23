

# lab4_trap

关于第一个实验只是回答一些问题，

**Q1:Which registers contain arguments to functions? For example, which register holds 13 in main's call to printf?**

函数参数寄存器为a0~a7,printf()中的13存在a2。

**Q2:Where is the call to function f in the assembly code for main? Where is the call to g? (Hint: the compiler may inline functions.)**

并没有直接的调用，g(x)被内联到了f(x)中（相当于代码直接插入，而非函数调用），而f(x)又被内联到了main中

**Q3:At what address is the function printf located?**

jalr	1536(ra) # 630 <printf>

**Q4:What value is in the register ra just after the jalr to printf in main?**

使用jalr跳转printf后，需要保存返回地址到ra，以供printf结束后回到main继续执行指令。
所以jalr会将PC+4，也就是下一条指令的地址存入ra中。
当前PC指向0x34，故ra=PC+4=0x38
auipc ra, 0x0      :auipc（Add Upper Immediate to PC）：将立即数的高20位与PC（程序计数器）相加，结果存入目标寄存器。    ra：目标寄存器，通常用于存储返回地址。
0x0：立即数的高20位，这里为0。  这条指令的作用是将PC的高20位与0相加，结果存入ra寄存器。由于立即数为0，ra的值就是当前PC的值。
jalr 1536(ra)       :jalr（Jump and Link Register）：跳转到寄存器中的地址加上偏移量，并将返回地址存入目标寄存器。
1536(ra)：偏移量1536加上ra寄存器中的值。        这条指令的作用是跳转到ra + 1536的地址，并将返回地址（下一条指令的地址）存入ra寄存器。



**Q5:Run the following code.**
	                        `unsigned int i = 0x00646c72;`
	                        `printf("H%x Wo%s", 57616, &i);`
    **What is the output? Here's an ASCII table that maps bytes to characters.**
**The output depends on that fact that the RISC-V is little-endian.** 
**If the RISC-V were instead big-endian what would you set i to in order to yield the same output? Would you need to change 57616 to a different value?**

观看printf()源代码

![](..\pic\Snipaste_2025-03-20_20-00-43.png)



​		首先将参数12和13分别转入a1和a2寄存器中，这里因为f函数和g函数都非常简单，所以无需调用。

然后就是当前pc值加1968（十进制）得到0x7d8地址，指向用户空间data区并该区域的指针存到a0寄存器，即参数一字符串。



![](C:\Users\Jack\Desktop\os note\pic\Snipaste_2025-03-20_20-11-13.png)



​		我们通过调试可以得到从0x7d8地址往后六个字节的内容，分别是是 25 64 20 25 0a 00，对应的字符分别是%	d	(空格)	%	d	（换行键）	空字符（字符串结束标记）

所以这个就是**大端**结构	（在大端中，最高有效字节位于最低内存地址，而最低有效字节位于最高内存地址。这类似于我们阅读多位数字时的顺序，先读最高位。）

[ASCII码](https://c.biancheng.net/c/ascii/)

回到本题，57616对应16进制为e110，然后i为0x00646c72,即因为为大端所以从低地址到高地址分别为72 6c 64 00,对应asc2码为r 	l	d	'/0'	然后将低地址传到%x打印出来则输出为`'He110 World'/0''`





**Q5:In the following code, what is going to be printed after `'y='`? (note: the answer is not a specific value.) Why does this happen?`printf("x=%d y=%d", 3);`**



answers：根据call的汇编代码
li a2,13
li a1,12
可以看出，printf的参数从a1、a2……中取，所以这里应该也是a2寄存器中的值





## Backtrace 

![](..\pic\stack2.png)

- 栈由高地址向低地址扩展，栈顶为sp。
- 每个函数栈帧中，依此存储返回地址、前一个栈帧的帧指针、保存的寄存器和局部变量等信息。
- s0寄存器中保存着**当前栈帧**的帧指针fp：指向当前栈帧的基址
- fp-16指向的位置保存**上一个栈帧**的fp









## Alarm

#### test0

错误记录，小小的细节

正确代码：

```c
uint64
sys_sigalarm(void){
 if( argint(0, &myproc()->interval)<0|| argaddr(1,&myproc()->handler)<0||myproc()->interval<0)
 return -1;
 myproc()->tick=0;
 return 0;
```

错误代码:

```c
uint64
sys_sigalarm(void){
 if( argint(0, &myproc()->interval)<0|| argaddr(1,(uint64*)(myproc()->handler))<0||myproc()->interval<0)
 return -1;
 myproc()->tick=0;
 return 0;
```

test0难度还可以唯一不懂的是，timer interruption是怎么操作的，什么时候开始中断，和用户进程如何搭配运行的。

这个知识点还是看后面内容吧。

注意，在allocatepro里初始化进程的属性，在freepro也是。而在sys_sigalarm就是要对该进程的属性赋值为我们想要的，就是我们上面错误的代码。

### 3月20日

上午一直在弄gdb调试，首先是打算调用户程序的，就是想用gdb调试实验里面要求查看的call的用户代码。

第一个问题就是 如何在call用户空间打断点，因为一开始我猜测默认在sh用户空间打断点，而我们要在call打断点，则需要file user/_call这个命令。

第二个问题哪就是一直出现，打了断点就有Cannot access memory at address 这个问题，我觉得可能是二进制目标文件的问题于是重新编译，但是又打c后又有program can't run的问题，上网上搜才得以解决

[第一](https://zhuanlan.zhihu.com/p/251366985)

[第二](https://zhuanlan.zhihu.com/p/645025872)