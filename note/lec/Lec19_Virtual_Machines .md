# Lec 19: Virtual Machines 笔记

## 19.1 Why Virtual Machine?

​		在架构的最底层，位于硬件之上存在一个Virtual Machine Monitor（VMM），它取代了标准的操作系统内核。

​		VMM的主要目的是提供对计算机的模拟，这样你可以不做修改就启动普通的Linux，普通的Windows系统，并运行在虚拟机内，并且不用担心任何奇怪的事情发生。

​		那么人们为什么会想要使用虚拟机呢？实际中有很多原因使得人们会在一个计算机上运行多个相互独立的操作系统。在一个大公司里面，你需要大量的服务，例如DNS，Firewall等等，但是每个服务并没有使用太多的资源，所以单独为这些服务购买物理机器有点浪费，但是将这些低强度的服务以虚拟机的形式运行在一个物理机上可以节省时间和资金。

## 19.2 Trap-and-Emulate --- Trap

我们该如何构建我们自己的VMM呢？

* 一种广泛使用的策略是在真实的CPU上运行Guest指令。

  如果我们要在VMM之上运行XV6，我们需要先将XV6的指令加载到内存中，之后再跳转到XV6的第一条指令，这样你的计算机硬件就能直接运行XV6的指令。

  当然，这要求你的计算机拥有XV6期望的处理器，但是实际中你又不能直接这么做，因为当你的Guest操作系统执行了一个privileged指令。

  我们将Guest kernel按照一个Linux中的普通用户进程来运行，所以Guest kernel现在运行在User mode，而在User mode加载SATP寄存器是个非法的操作，这会导致我们的程序（注，也就是虚拟机）crash。

  所以我们不能直接简单的在真实的CPU上运行Guest kernel。

* 技巧：

  1.首先将Guest kernel运行在宿主机的User mode

  一旦Guest操作系统需要使用privileged指令，因为它当前运行在User mode而不是Supervisor mode，会使得它触发trap并走回到我们的VMM中（注，在一个正常操作系统中，如果在User mode执行privileged指令，会通过trap走到内核，但是现在VMM替代了内核），之后我们就可以获得控制权。

  

  

  



## 19.3 Trap-and-Emulate --- Emulate

VMM会为每一个Guest维护一套虚拟状态信息。

所以VMM里面会维护虚拟的STVEC寄存器，虚拟的SEPC寄存器以及其他所有的privileged寄存器。当Guest操作系统运行指令需要读取某个privileged寄存器时，首先会通过trap走到VMM，因为在用户空间读取privileged寄存器是非法的。之后VMM会检查这条指令并发现这是一个比如说读取SEPC寄存器的指令，之后VMM会模拟这条指令，并将自己维护的虚拟SEPC寄存器，拷贝到trapframe的用户寄存器中（注，有关trapframe详见Lec06，这里假设Guest操作系统通过类似“sread a0, sepc”的指令想要将spec读取到用户寄存器a0）。之后，VMM会将trapframe中保存的用户寄存器拷贝回真正的用户寄存器，通过sret指令，使得Guest从trap中返回。

在这种虚拟机的实现中，Guest整个运行在用户空间，任何时候它想要执行需要privilege权限的指令时，会通过trap走到VMM，VMM可以模拟这些指令。这种实现风格叫做Trap and Emulate。

VMM怎么知道Guest当前的mode呢？当Guest从Supervisor mode返回到User mode时会执行sret指令，而sret指令又是一个privileged指令，所以会通过trap走到VMM，进而VMM可以看到Guest正在执行sret指令，并将自己维护的mode从Supervisor变到User。虚拟状态信息中保存的另外一个信息是hartid，它代表了CPU核的编号。即使通过privileged指令，也不能直接获取这个信息，VMM需要跟踪当前模拟的是哪个CPU。



## 19.4 Trap-and-Emulate --- Page Table

第一个部分是Guest操作系统在很多时候会修改SATP寄存器，我们不能让Guest操作系统简单的设置SATP寄存器。

所以当Guest设置SATP寄存器时，真实的过程是，我们不能直接使用Guest操作系统的Page Table，VMM会生成一个新的Page Table来模拟Guest操作系统想要的Page Table。

我们不能直接使用Guest物理地址，因为它们不对应真实的物理内存地址。

相应的，VMM会为每个虚拟机维护一个映射表，将Guest物理内存地址映射到真实的物理内存地址，我们称之为主机物理内存地址。当Guest向SATP寄存器写了一个新的Page Table时，在对应的trap handler中，VMM会创建一个Shadow Page Table，Shadow Page Table的地址将会是VMM向真实SATP寄存器写入的值Shadow Page Table由上面两个Page Table组合而成，所以它将gva映射到了hpa。Shadow Page Table是这么构建的：

- 从Guest Page Table中取出每一条记录，查看gpa。
- 使用VMM中的映射关系，将gpa翻译成hpa。
- 再将gva和hpa存放于Shadow Page Table。

在创建完之后，VMM会将Shadow Page Table设置到真实的SATP寄存器中，再返回到Guest内核中



## 19.5 Trap-and-Emulate --- Devices

三种策略：

第一种是，模拟一些需要用到的并且使用非常广泛的设备，例如磁盘。就是说，Guest并不是拥有一个真正的磁盘设备，只是VMM使得与Guest交互的磁盘看起来好像真的存在一样。这里的实现方式是，Guest操作系统仍然会像与真实硬件设备交互一样，通过Memory Map控制寄存器与设备进行交互。

第二种策略是提供虚拟设备，而不是模拟一个真实的设备。过在VMM中构建特殊的设备接口，可以使得Guest中的设备驱动与VMM内支持的设备进行高效交互。(客户操作系统中设计了一个**专门对接 VMM 实现的虚拟设备的设备驱动**, 这样客户操作系统也意识到自己是在与虚拟设备交互且在虚拟机上运行)

第三个策略是对于真实设备的pass-through，这里典型的例子就是网卡。