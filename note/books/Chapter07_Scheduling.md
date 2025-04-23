# scheduling

## Multiplexing

v6 通过两种场景在 CPU 上切换进程来实现多路复用。
**第一种场景**：当进程等待设备或管道 I/O 完成、等待子进程退出，或通过 `sleep` 系统调用主动休眠时，xv6 利用睡眠与唤醒（`sleep` & `wakeup`）机制进行切换。
**第二种场景**：xv6 会定期强制切换进程，以处理那些长时间占用 CPU 而不休眠的计算密集型进程。

这种多路复用机制让每个进程仿佛独占了 CPU，就像 xv6 通过内存分配器和硬件页表让每个进程仿佛拥有独立内存一样。

实现多路复用面临以下挑战：

1. **上下文切换的实现**：虽然上下文切换的概念简单，但其底层代码是 xv6 中最晦涩的部分。
2. **透明的强制切换**：xv6 使用硬件定时器中断驱动上下文切换，这对用户进程是透明的。
3. **多核竞争与锁机制**：所有 CPU 共享同一进程集合，需通过锁避免竞态条件。
4. **资源释放的悖论**：进程退出时必须释放内存等资源，但它不能自行释放内核栈（因为此时仍在用它执行代码）。
5. **多核状态跟踪**：每个 CPU 核心需记录当前运行的进程，以确保系统调用正确修改目标进程的内核状态。
6. **睡眠与唤醒的竞态**：`sleep` 和 `wakeup` 需谨慎设计，避免因竞态导致唤醒信号丢失。

xv6 试图以最简单的方式解决这些问题，但最终的代码实现仍非常复杂。

![](..\pic\scheduling.png)



## Code: Context switching

​		xv6 通过以下步骤实现用户进程间的切换（如图7.1所示）：

1. **用户态到内核态的转换**：通过系统调用或中断进入旧进程的内核线程。
2. **上下文切换到当前 CPU 的调度器线程**：内核线程通过 `swtch` 函数切换到调度器线程。
3. **上下文切换到新进程的内核线程**：调度器选择新进程后，再次通过 `swtch` 切换到其内核线程。
4. **陷阱返回用户态**：从新进程的内核线程返回到用户态继续执行。

xv6 的调度器为每个 CPU 核心维护一个**专用调度器线程**（包含独立的寄存器和栈），这是因为直接在旧进程的内核栈上执行调度器是不安全的：其他 CPU 核心可能唤醒该进程并运行它，若共享栈会导致灾难性后果。本节重点分析内核线程与调度器线程之间的切换机制。





###  1.**上下文切换的本质**

​	**寄存器保存与恢复**：切换线程需保存旧线程的 CPU 寄存器状态，并加载新线程的寄存器状态。其中，栈指针（`sp`）和程序计数器（`pc`）的切换是关键——这决定了 CPU 执行哪段代码及使用哪个栈。函数 swtch执行对内核线程转变的保存和恢复。`swtch` 不直接感知线程，仅操作 **32 个 RISC-V 寄存器的集合**（即 `struct context`）。当进程让出 CPU 时，其内核线程调用 `swtch` 保存自身上下文到 `p->context`，并切换到调度器的上下文 `cpu->scheduler`。

###  2.**上下文结构体**

- `struct context` 定义（kernel/proc.h:2）：

  ```c
  struct context {
    uint64 ra;          // 返回地址
    uint64 sp;          // 栈指针
    uint64 s0, s1, ...; // 被调用者保存的寄存器（callee-saved registers）
    // ... 其他寄存器
  };
  ```

  - `ra`（Return Address）：保存 `swtch` 的返回地址，确保切换后执行流正确。
  - `sp`：栈指针，切换线程时自动切换栈。
  - **仅保存被调用者保存的寄存器**（如 `s0`-`s11`），调用者保存的寄存器（如 `a0`-`a7`）由编译器生成的代码在调用 `swtch` 前压栈保存。

### 3.切换流程示例

假设进程 A 因中断或系统调用进入内核态，最终调用 `yield()` 让出 CPU：

1. **`yield()` → `sched()`**：
   - `sched()` 调用 `swtch(&p->context, &cpu->scheduler)`，保存进程 A 的上下文到 `p->context`，切换到调度器的上下文 `cpu->scheduler`。
2. **调度器线程执行**：
   - 调度器通过 `swtch` 切换到新进程 B 的上下文（如 `swtch(&cpu->scheduler, &p->context)`）。
   - `swtch` 恢复进程 B 的寄存器（包括 `ra` 和 `sp`），返回到进程 B 内核线程上次调用 `swtch` 的位置（例如 `scheduler()` 中的某个点）。



### 4.细节

- **`ra` 寄存器的魔法**：
  - `swtch` 保存 `ra`，使得切换后能返回到新线程上次调用 `swtch` 的位置。例如：
    - 进程 A 的 `swtch` 调用点：在 `sched()` 中。
    - 调度器的 `swtch` 调用点：在 `scheduler()` 循环中。
  - 当 `swtch` 从调度器上下文返回时，实际返回到 `scheduler()` 函数，而非原来的 `sched()`。
- **栈的切换**：
  - 旧进程的内核栈 → 调度器专用栈 → 新进程的内核栈。
  - 调度器的专用栈避免了多核竞争问题。





##### 5. **多核同步与安全性**

- **每个 CPU 核心独立的调度器**：
  - 每个核心维护 `struct cpu`，包含调度器线程的上下文和栈。
  - 进程的上下文（`p->context`）和调度器上下文（`cpu->scheduler`）严格分离。
- **锁机制**：
  - 进程表（`ptable`）等共享资源通过自旋锁（如 `ptable.lock`）保护，确保调度操作的原子性。



## code : Scheduling

上一节讨论了 `swtch` 的底层细节，现在我们将 `swtch` 视为既定机制，分析如何从**一个进程的内核线程**通过调度器切换到**另一个进程的内核线程**。
xv6 的调度器以**每个 CPU 核心一个专用线程**的形式存在，每个线程运行 `scheduler` 函数。调度器的职责是选择下一个要运行的进程。若一个进程需要让出 CPU，它必须：

1. **获取自身的进程锁 `p->lock`**；
2. **释放其他持有的锁**；
3. **更新自身状态（如 `p->state`）**；
4. **调用 `sched`**。

这一流程体现在 `yield`、`sleep` 和 `exit` 函数中。`sched` 会验证以下条件（确保正确性）：

- 进程已持有 `p->lock`；
- 中断已被禁用（因持有锁时需关闭中断）；
- 当前 CPU 没有持有其他锁（避免死锁）。

随后，`sched` 调用 `swtch` 保存当前上下文到 `p->context`，并切换到调度器的上下文 `cpu->scheduler`。`swtch` 返回时，调度器继续循环查找可运行进程，切换到该进程，循环往复。







#### 核心机制详解

##### 1. **锁的非常规管理**

- **跨 `swtch` 持有 `p->lock`**：
  - 调用 `swtch` 的线程必须持有 `p->lock`，锁的控制权在切换后转移到目标代码。
  - **违反常规锁规则**：通常，获取锁的线程需负责释放，但此处需打破惯例，因为 `p->lock` 保护进程状态和上下文的**不变式**（invariants）。
  - **示例问题**：若在 `yield` 设置进程状态为 `RUNNABLE` 后、`swtch` 切换前未持有锁，其他 CPU 可能选择运行该进程，导致两个 CPU 共享同一内核栈，引发灾难。

##### 2. **协程（Coroutines）模式**

- **`sched` 与 `scheduler` 的协程关系**：
  - 内核线程仅在 `sched` 中让出 CPU，且总是切换到 `scheduler` 的固定位置（通过 `swtch`）。
  - `scheduler` 几乎总是切换到之前调用过 `sched` 的内核线程。
  - **调用模式**：`scheduler` → `swtch` → `sched` → `swtch` → `scheduler`，形成循环，二者互为协程。

##### 3. **新进程的启动与 `forkret`**

- **新进程的首次切换**：
  - `allocproc` 初始化新进程时，设置其上下文的 `ra` 寄存器为 `forkret`（kernel/proc.c:508）。
  - **目的**：新进程首次通过 `swtch` “返回”到 `forkret`，释放 `p->lock`，并准备返回用户态（模拟 `fork` 返回）。
  - **必要性**：若不通过 `forkret` 释放锁，新进程无法安全返回到用户空间。

##### 4. **调度器循环与不变式维护**

- **调度器主循环（kernel/proc.c:438）**：

  ```c
  void scheduler(void) {
    struct proc *p;
    for (;;) {
      intr_off(); // 关闭中断
      for (p = proc; p < &proc[NPROC]; p++) { // 遍历进程表
        if (p->state == RUNNABLE) { // 找到可运行进程
          p->state = RUNNING;
          c->proc = p; // 设置当前 CPU 运行的进程
          swtch(&c->scheduler, &p->context); // 切换到进程的上下文
          c->proc = 0; // 清除当前 CPU 的进程引用
        }
      }
    }
  }
  ```

  - **关键步骤**：
    1. 关闭中断，避免并发问题。
    2. 遍历进程表，查找 `RUNNABLE` 状态的进程。
    3. 切换上下文前设置进程状态为 `RUNNING`，并更新 `c->proc`。
    4. 调用 `swtch` 切换到目标进程的内核线程。

- **不变式（Invariants）**：

  - **对 `RUNNING` 进程**：
    - CPU 寄存器必须保存进程的当前状态（未被 `swtch` 保存到上下文）。
    - `c->proc` 必须指向该进程（确保定时中断的 `yield` 能正确切换）。
  - **对 `RUNNABLE` 进程**：
    - 寄存器状态必须保存在 `p->context`（不在实际寄存器中）。
    - 没有 CPU 正在使用该进程的内核栈。
    - 没有 CPU 的 `c->proc` 指向该进程。
  - **锁的作用**：`p->lock` 在不变式不成立时被持有，例如在修改进程状态或上下文期间。

##### 5. **锁的跨线程传递**

- **示例：`yield` 和 `scheduler` 的协作**：
  - `yield` 获取 `p->lock` → 修改进程状态为 `RUNNABLE` → 调用 `sched` → `swtch` 切换到调度器。
  - 调度器在自身栈上运行，清除 `c->proc` 后释放 `p->lock`。
  - **意义**：确保在进程状态和上下文修改期间，不变式始终被锁保护。

## Sleep and wakeup

```c
 struct semaphore{
  struct spinlock lock;
  int count;
  };

void 
V(structsemaphore *s){
     acquire(&s->lock);
     s->count+=1;
     wakeup(s);
 	 release(&s->lock);
 }


void
P(structsemaphore *s){
    acquire(&s->lock);
 	while(s->count==0)
 	sleep(s,&s->lock);
 	s->count-=1;
 	release(&s->lock);
}
```

以上是最终代码。

#### **1. 初始忙等待实现（刚开始的代码）

- **问题**：消费者线程通过 `while(s->count == 0)` 忙等待，导致 CPU 空转，效率低下。
- **原因**：没有让出 CPU 的机制，消费者在等待时无法执行其他任务。

#### **2. 引入 sleep/wakeup 的尝试（第二段代码）**

- **改进**：消费者在 `count == 0` 时调用 `sleep(s)` 让出 CPU，生产者通过 `wakeup(s)` 唤醒消费者。
- **问题**：存在 **丢失唤醒（Lost Wakeup）**：
  - 若消费者在检查 `count == 0`（行212）后、调用 `sleep(s)`（行213）前被抢占，此时生产者可能修改 `count` 并调用 `wakeup(s)`。
  - 由于消费者尚未睡眠，`wakeup` 无效。当消费者恢复执行并调用 `sleep` 后，将永远等待一个已发生的唤醒事件。

#### **3. 加锁改进（第三段代码）**

- **改进**：在 `P` 操作中先获取锁，再检查 `count`。
- **问题**：**死锁**：
  - 消费者在持有锁的情况下调用 `sleep`，导致生产者无法获取锁，无法执行 `V` 操作唤醒消费者。

#### **4. 最终正确实现**

- **关键改进**：`sleep` 的接口修改为接收锁参数 `sleep(s, &s->lock)`。
- **流程**：
  1. **消费者（P）**：
     - 获取锁（行412）。
     - 检查 `count`，若为0，调用 `sleep(s, &s->lock)`：
       - 在 `sleep` 内部，原子地释放锁，并将消费者标记为等待状态。
     - 被唤醒后，`sleep` 自动重新获取锁，继续循环检查 `count`。
     - 若 `count > 0`，减少计数并释放锁。
  2. **生产者（V）**：
     - 获取锁（行403）。
     - 增加 `count`，调用 `wakeup(s)` 唤醒等待的消费者。
     - 释放锁。
- **为什么能避免丢失唤醒**：
  - 消费者在持有锁时检查 `count`，确保检查与进入睡眠的原子性。
  - `sleep` 内部释放锁的操作与标记进程为睡眠状态是原子的，确保生产者只能在消费者完全进入睡眠后执行 `wakeup`。
  - `wakeup` 在持有锁时调用，保证生产者修改 `count` 和唤醒操作的原子性。

------

### **核心机制**

1. **条件变量的原子性**：
   - 检查条件（`count == 0`）和进入睡眠必须在同一锁的保护下进行。
   - `sleep` 必须原子地释放锁并进入等待状态，避免竞争条件。
2. **唤醒的可见性**：
   - `wakeup` 在持有锁时调用，确保唤醒操作能观察到最新的 `count` 值。
3. **循环检查的必要性**：
   - 使用 `while (s->count == 0)` 而非 `if`，防止虚假唤醒（Spurious Wakeup）或多个消费者竞争。

------

### **总结**

- **丢失唤醒的根源**：检查和睡眠操作的非原子性。
- **解决方案**：通过锁保护条件检查，并让 `sleep` 原子地释放锁和进入等待。
- **最终代码的正确性**：
  - 消费者在持有锁时检查条件，确保不会被生产者干扰。
  - `sleep` 和 `wakeup` 的协作保证了唤醒不会丢失。

该模式是条件变量（Condition Variable）的雏形，是现代操作系统同步机制（如 Linux 的 `pthread_cond_wait`）的基础。

## Code : Sleep and wake up

xv6 的 `sleep`（位于 `kernel/proc.c:529`）和 `wakeup`（位于 `kernel/proc.c:560`）提供了上一示例中的接口，它们的实现（及使用规则）确保了**不会发生丢失唤醒（Lost Wakeup）**。核心思想是：

- **`sleep`** 将当前进程标记为 `SLEEPING`，然后调用 `sched` 释放 CPU。
- **`wake up`** 查找在指定**等待通道（wait channel）**上睡眠的进程，并将其标记为 `RUNNABLE`。
- 调用 `sleep` 和 `wakeup` 的双方可以使用任何约定的数值作为通道标识符。xv6 通常使用与等待相关的内核数据结构地址作为通道。

#### **`sleep` 的实现细节**

1. **获取进程锁**：
   `sleep` 首先获取当前进程的锁 `p->lock`（`kernel/proc.c:540`）。此时，进程同时持有 `p->lock` 和传入的条件锁 `lk`。
   - **条件锁 `lk` 的必要性**：在调用 `sleep` 前（例如 `P` 操作中），`lk` 确保没有其他进程（例如执行 `V` 操作的进程）能提前调用 `wakeup(chan)`。
   - **释放 `lk` 的安全性**：一旦 `sleep` 持有 `p->lock`，便可安全释放 `lk`。其他进程可能开始调用 `wakeup(chan)`，但 `wakeup` 会等待获取 `p->lock`，直到 `sleep` 完成将进程置为睡眠状态，从而避免唤醒被遗漏。
2. **标记进程状态**：
   `sleep` 记录等待通道 `chan`，将进程状态改为 `SLEEPING`，并调用 `sched` 释放 CPU（`kernel/proc.c:544-547`）。
   - **关键点**：在进程被标记为 `SLEEPING` 之前，`p->lock` 不会被释放（由调度器处理），这确保了唤醒操作不会错过进程状态的变更。

#### **`wakeup` 的实现细节**

1. **条件锁的持有**：
   调用 `wakeup` 的进程**必须持有条件锁**（例如信号量的锁）。
   - `wakeup` 遍历进程表（`kernel/proc.c:560`），检查每个进程的 `p->lock`，以操作其状态并确保与 `sleep` 的同步。
   - 当 `wakeup` 发现一个处于 `SLEEPING` 状态且匹配 `chan` 的进程时，将其状态改为 `RUNNABLE`。下次调度时，该进程将被执行。

#### **锁规则的保障**

- **睡眠进程** 在检查条件到标记为 `SLEEPING` 的期间，始终持有**条件锁 `lk`** 或**进程锁 `p->lock`**（或两者）。
- **唤醒进程** 在 `wakeup` 循环中持有这两个锁。
  因此，唤醒者（waker）要么在消费线程检查条件前使条件变为真，要么在目标进程被标记为 `SLEEPING` 后检查它，确保唤醒操作必然生效（除非其他操作已唤醒该进程）。

#### **多进程等待同一通道**

- 例如，多个进程从管道读取数据时，一次 `wakeup` 会唤醒所有等待进程。
- **竞争处理**：
  第一个运行的进程将获取条件锁（如管道锁），读取数据；其他进程因无数据可读需重新睡眠。
  - 对这些进程来说，唤醒是“虚假的（spurious）”，因此 `sleep` **必须始终在条件检查循环中被调用**。

#### **通道冲突的容错性**

- 若多个不相关的 `sleep/wakeup` 意外使用相同通道，只会导致虚假唤醒，但循环条件检查可容忍此问题。
- **优势**：`sleep/wakeup` 轻量（无需专用数据结构）且提供间接性（调用者无需知晓具体进程）。



## pipe

### **管道的基本结构**

每个管道由 `struct pipe` 表示，

![](..\pic\pipe.png)

- **环形缓冲区**：写入数据从 `buf[0]` 到 `buf[PIPESIZE-1]` 循环填充，通过取模运算定位索引（如 `buf[nread % PIPESIZE]`）。
- **计数不循环**：`nread` 和 `nwrite` 为累加值，通过比较 `nwrite == nread + PIPESIZE` 判断缓冲区是否满，`nwrite == nread` 判断是否空



### **生产者（`pipewrite`）的工作流程**

<img src="..\pic\pipe2.png" style="zoom: 67%;" />

1. **获取锁**：
   `pipewrite` 首先获取管道的锁 `pi->lock`，确保对缓冲区、`nread` 和 `nwrite` 的独占访问。
2. **写入数据**：
   循环将数据逐个字节写入缓冲区
   - **缓冲区满时的处理**：
     - 调用 `wakeup(&pi->nread)` 唤醒可能等待的读者。
     - 调用 `sleep(&pi->nwrite, &pi->lock)` 释放锁并进入睡眠，等待读者消费数据后唤醒。
3. **最终唤醒与释放锁**：
   写入完成后，再次唤醒读者并释放锁









### **消费者（`piperead`）的工作流程**

![](..\pic\pipe1.png)

1. **获取锁**：
   `piperead` 尝试获取 `pi->lock`，若锁被 `pipewrite` 持有，则自旋等待。
2. **读取数据**：
   检查是否有数据可读（`nread != nwrite`），若缓冲区为空则进入睡眠
3. **唤醒生产者并释放锁**：
   读取完成后，唤醒可能因缓冲区满而睡眠的写者





### **同步机制的核心设计**

#### **1. 锁的原子性保护**

- **共享资源保护**：通过 `pi->lock` 确保对缓冲区、`nread` 和 `nwrite` 的原子访问。
- **睡眠与唤醒的协调**：
  - `sleep` 调用时释放锁，允许其他进程（如读者/写者）获取锁并修改状态。
  - `wakeup` 在持有锁时调用，确保唤醒操作不会遗漏进程状态变更。

#### **2. 分离的睡眠通道**

- **读/写使用不同通道**：
  - 读者睡眠在 `&pi->nread`，写者睡眠在 `&pi->nwrite`。
  - **优势**：减少不必要的唤醒。例如，当写者唤醒时，仅唤醒读者，而非所有等待者。

#### **3. 循环检查条件**

- **防止虚假唤醒**：

  ```c
  while (pi->nwrite == pi->nread + PIPESIZE) { // 写者检查缓冲区满
    sleep(&pi->nwrite, &pi->lock);
  }
  ```

  - 即使被唤醒，仍需重新检查条件，确保缓冲区确实可写/可读。

#### **4. 唤醒时机的控制**

- **写者唤醒读者**：当缓冲区有数据时（如写入新数据或写者睡眠前），调用 `wakeup(&pi->nread)`。
- **读者唤醒写者**：当释放缓冲区空间后，调用 `wakeup(&pi->nwrite)`。





### **示例场景**

1. **缓冲区满时的写入**：
   - 写者 `pipewrite` 发现缓冲区满，调用 `wakeup(&pi->nread)` 唤醒读者。
   - 写者调用 `sleep(&pi->nwrite, &pi->lock)` 释放锁并进入睡眠。
   - 读者 `piperead` 获取锁，读取数据，增加 `nread`，调用 `wakeup(&pi->nwrite)` 唤醒写者。
   - 写者被唤醒后，继续写入剩余数据。
2. **多读者/写者竞争**：
   - 多个读者等待 `&pi->nread`，多个写者等待 `&pi->nwrite`。
   - 一次 `wakeup` 可能唤醒所有等待同一通道的进程，但只有第一个获取锁的进程能处理数据，其他进程因条件不满足重新睡眠。



### **设计优势**

1. **高效睡眠/唤醒**：
   - 轻量级的通道标识（如变量地址）避免了复杂的数据结构。
   - 分离的睡眠通道减少了不必要的唤醒。
2. **避免死锁与丢失唤醒**：
   - 通过锁的原子操作和 `sleep` 的锁传递机制，确保检查和睡眠的原子性。
   - `wakeup` 在持有锁时调用，保证唤醒操作可见性。
3. **环形缓冲区的优雅处理**：
   - 通过累加计数和取模运算，简化了缓冲区满/空的判断及索引计算。

------

### **总结**

xv6 的管道实现通过 `sleep`/`wakeup` 和锁机制，高效协调了生产者和消费者：

- **锁** 保护共享状态，确保原子性。
- **分离的睡眠通道** 减少竞争，提升唤醒效率。
- **循环条件检查** 应对虚假唤醒，保证正确性。
  这一设计是经典同步问题的教科书级解决方案，为理解操作系统同步机制提供了坚实基础。

##  Code: Wait, exit, and kill

### **1. 父子进程的同步：`exit` 与 `wait`**

#### **核心流程**

- **子进程退出（`exit`）**：
  1. **记录退出状态**：设置进程的退出状态码。
  2. **释放资源**：关闭打开的文件、释放内存等（但不释放进程描述符 `struct proc`）。
  3. **移交子进程**：若父进程已终止，将子进程交给 `init` 进程（PID=1），确保所有进程最终被回收。
  4. **唤醒父进程**：调用 `wakeup(p->parent)` 通知父进程（若父进程在 `wait` 中睡眠）。
  5. **标记为僵尸（ZOMBIE）**：进程状态变为 `ZOMBIE`，等待父进程回收。
  6. **永久放弃 CPU**：调用 `sched` 切换到其他进程，不再执行。
- **父进程等待（`wait`）**：
  1. **获取 `wait_lock`**：此锁用于同步父子进程，防止父进程错过子进程的唤醒。
  2. **扫描进程表**：查找处于 `ZOMBIE` 状态的子进程。
  3. **回收资源**：若找到僵尸子进程，释放其 `struct proc`，复制子进程的退出状态码。
  4. **无子进程可回收时**：调用 `sleep` 等待任意子进程退出，随后重新扫描。

#### **同步机制设计**

- **锁的顺序**：
  - `wait` 先获取 `wait_lock`，再获取子进程的 `p->lock`，避免死锁。
  - `exit` 同样按此顺序获取锁，确保父子进程操作的原子性。
- **ZOMBIE 状态的作用**：
  - 子进程退出后保持 `ZOMBIE` 状态，直到父进程调用 `wait` 回收。
  - 父进程在 `wait` 中检查子进程状态，确保不会漏掉已退出的子进程。
- **唤醒的安全性**：
  - `exit` 在设置状态为 `ZOMBIE` **前** 唤醒父进程，看似不安全，但实际因父进程需获取子进程的 `p->lock` 才能处理，故父进程在子进程完全标记为 `ZOMBIE` 后才会执行回收。

------

### **2. 进程终止的扩展：`kill` 机制**

#### **`kill` 的实现**

- **非立即终止**：`kill` 不直接终止目标进程，而是设置 `p->killed` 标志。
  - **原因**：目标进程可能正在执行敏感操作（如文件系统更新），直接终止会导致数据不一致。
  - **唤醒睡眠进程**：若目标进程在 `sleep` 中，`kill` 调用 `wakeup` 使其返回。
- **安全终止检查**：
  - **用户空间**：目标进程在返回用户空间前，通过系统调用或中断进入内核，`usertrap` 检查 `p->killed` 并调用 `exit`。
  - **内核空间**：在 `sleep` 循环中检查 `p->killed`，若置位则放弃当前操作，逐层返回至 `exit`。

#### **`sleep` 循环中的检查**

- **通用模式**：

  ```c
  while (condition) {
    if (p->killed) {
      // 释放资源并退出
      exit();
    }
    sleep(...);
  }
  ```

  - 确保被 `kill` 的进程能及时终止，即使正在等待条件（如管道数据）。

- **例外情况**：

  - **原子性操作**：如磁盘 I/O 期间不检查 `p->killed`，避免文件系统处于不一致状态。

    ```c
    // virtio 磁盘驱动示例（kernel/virtio_disk.c）
    while (未完成 I/O) {
      sleep(...); // 不检查 p->killed
    }
    ```

  - 进程在完成当前系统调用后，由 `usertrap` 统一处理终止。

------

### **3. 关键设计思想**

#### **避免竞争与死锁**

- **锁的层级**：`wait_lock` 作为全局条件锁，协调多个父进程的 `wait` 操作。
- **状态变更原子性**：`exit` 和 `wait` 通过锁保护进程状态，确保父子进程同步的正确性。

#### **延迟终止的合理性**

- **资源安全**：允许进程完成关键操作后再退出，避免破坏内核数据结构。
- **统一出口**：通过 `usertrap` 集中处理终止，简化错误路径。

#### **僵尸进程的必要性**

- **信息保留**：`ZOMBIE` 状态保留退出状态码，供父进程查询。
- **资源回收责任**：强制父进程显式调用 `wait`，防止资源泄漏。

------

### **总结**

xv6 通过 `exit`、`wait` 和 `kill` 实现了进程的安全终止与资源回收：

- **`exit` 与 `wait` 协作**：通过锁和状态机保证父子进程同步，避免丢失唤醒。
- **`kill` 的延迟终止**：标志位 + 唤醒机制平衡了安全性与响应性。
- **僵尸进程**：作为中间状态，确保父进程能获取子进程终止信息。

这一设计体现了操作系统对进程生命周期的精细管理，是理解进程同步与资源管理的重要案例。







## process locking

### **1. 进程锁 `p->lock` 的核心作用**

`p->lock` 是 xv6 中最复杂的锁，保护进程结构体 `struct proc` 的多个关键字段和操作，主要功能如下：

#### **基本字段保护**

- **保护字段**：
  `p->state`（进程状态）、`p->chan`（睡眠通道）、`p->killed`（终止标志）、`p->xstate`（退出状态码）、`p->pid`（进程 ID）。
  - **必要性**：这些字段可能被其他进程或不同 CPU 核心的调度线程访问，需保证操作的原子性。

#### **高级功能与场景保护**

1. **进程槽分配竞争**
   - 防止多个核心同时分配 `proc[]` 数组中的空闲槽位时发生冲突。
2. **进程创建与销毁的隐蔽性**
   - 在进程创建（`fork`）或销毁（`exit`）期间，隐藏进程状态变化，避免其他线程观察到中间状态。
3. **防止父进程过早回收子进程**
   - 确保子进程在完全设置 `ZOMBIE` 状态并释放 CPU 前，父进程的 `wait` 不会错误回收。
4. **调度器竞态条件**
   - 防止进程设置 `RUNNABLE` 状态后、调用 `swtch` 切换上下文前，其他核心的调度器误判为可运行状态。
5. **单核心运行决策**
   - 确保同一时刻只有一个核心的调度器决定运行某个 `RUNNABLE` 进程，避免多核并发执行同一进程。
6. **中断安全**
   - 防止定时器中断在进程执行 `swtch` 时触发 `yield`，导致上下文切换不一致。
7. **`sleep`/`wakeup` 同步**
   - 与条件锁配合，避免 `wakeup` 遗漏正在调用 `sleep` 但尚未完全让出 CPU 的进程。
8. **`kill` 操作的安全性**
   - 防止目标进程在 `kill` 检查 `p->pid` 后、设置 `p->killed` 前退出并被重新分配，导致误操作。
   - 保证 `kill` 对 `p->state` 的检查和修改是原子的。

------

### **2. 全局等待锁 `wait_lock` 的作用**

#### **保护字段与协调机制**

- **保护字段**：`p->parent`（父进程指针）。
  - **写操作**：仅由进程的父进程修改（如 `exit` 中调整父子关系）。
  - **读操作**：进程自身或其他进程（如 `init` 继承孤儿进程）可能读取。

#### **核心功能**

1. **条件锁角色**
   - 当父进程调用 `wait` 等待任意子进程退出时，`wait_lock` 作为睡眠条件的锁，确保唤醒不丢失。
2. **退出序列化**
   - 子进程退出时需持有 `wait_lock` 或 `p->lock`，直到完成以下操作：
     a. 设置状态为 `ZOMBIE`。
     b. 唤醒父进程。
     c. 让出 CPU。
   - 防止父子进程并发执行 `exit` 和 `wait` 时的竞争。
3. **`init` 进程的可靠继承**
   - 当父进程先于子进程退出时，`wait_lock` 确保 `init` 进程（PID=1）能正确继承孤儿进程，并从 `wait` 中唤醒。

#### **设计权衡**

- **全局锁而非每进程锁**：
  - 进程在获取 `wait_lock` 前无法确定其父进程身份，全局锁简化了父子关系的动态管理。
  - 尽管可能引入竞争，但通过锁的粗粒度保护确保了正确性。



------



### **3. 锁协作示例**

#### **场景：子进程退出与父进程 `wait`**

1. **子进程 `exit`**：
   - 获取 `wait_lock` 和 `p->lock`。
   - 设置状态为 `ZOMBIE`，唤醒父进程，释放 CPU。
   - 释放锁的顺序确保父进程在子进程完全退出后执行回收。

```c
// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}
```







2. **父进程 `wait`**：

- 获取 `wait_lock`，扫描进程表查找 `ZOMBIE` 子进程。
- 若无子进程可回收，调用 `sleep` 并释放 `wait_lock`，等待唤醒。
- 被唤醒后重新获取锁，继续扫描。





```c
// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  // hold p->lock for the whole time to avoid lost
  // wakeups from a child's exit().
  acquire(&p->lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      // this code uses np->parent without holding np->lock.
      // acquiring the lock first would cause a deadlock,
      // since np might be an ancestor, and we already hold p->lock.
      if(np->parent == p){
        // np->parent can't change between the check and the acquire()
        // because only the parent changes it, and we're the parent.
        acquire(&np->lock);
        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&p->lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&p->lock);
          return pid;
        }
        release(&np->lock);
      }
    }
      
      
      
     // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&p->lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &p->lock);  //DOC: wait-sleep
  }
}

```







#### **场景：`kill` 终止进程**

- **`kill` 设置 `p->killed`**：
  - 在持有 `p->lock` 时检查并设置标志，确保目标进程不会在操作中途退出或状态不一致。
  - 若目标进程在睡眠中，唤醒它，使其在 `sleep` 循环中检测 `p->killed` 并退出。

```c

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

```









------

### **4. 关键设计思想**

#### **锁的职责分离**

- **`p->lock`**：聚焦进程内部状态与生命周期管理。
- **`wait_lock`**：协调父子进程关系及退出同步。

#### **原子性与可见性**

- 通过锁的严格顺序（如先 `wait_lock` 后 `p->lock`）避免死锁。
- 锁保护下的状态变更确保多核间内存操作的可见性。

#### **性能与正确性权衡**

- **全局锁的代价**：可能限制并发性，但简化了父子进程的动态关系管理。
- **细粒度锁的复杂性**：xv6 选择粗粒度锁以减少代码复杂度，适合教学目的。

------

### **总结**

xv6 通过 `p->lock` 和 `wait_lock` 的协同设计，实现了进程管理的并发安全：

- **`p->lock`** 作为进程的“守护锁”，覆盖状态、调度、终止等多场景。
- **`wait_lock`** 作为全局协调者，确保父子进程同步与资源回收的可靠性。
  这一设计体现了操作系统内核在并发控制中的经典模式，是理解进程生命周期管理的重要范例。











## real world

### **1. xv6 的调度策略：轮转调度（Round Robin）**

#### **基本机制**

- **轮转执行**：调度器依次选择 `RUNNABLE` 状态的进程运行，每个进程在时间片用完或主动让出 CPU 后被切换。
- **简单性**：实现简单，无需复杂优先级逻辑，适合教学和轻量级系统。

#### **对比实际操作系统的调度策略**

- **优先级调度**：允许高优先级进程优先执行，但引入复杂性：
  - **优先级反转（Priority Inversion）**：低优先级进程持有高优先级进程所需的锁，导致后者无法推进。
    - **示例**：火星探路者号因优先级反转导致系统重置。
  - **护航效应（Convoy Effect）**：多个高优先级进程因等待低优先级进程持有的共享锁形成长队列，降低系统吞吐量。
- **解决方案**：优先级继承、优先级天花板等机制，但增加调度器复杂度。

------

### **2. 同步机制：`sleep` 与 `wakeup`**

#### **xv6 的实现**

- **显式锁避免丢失唤醒**：在多核环境下，`sleep` 调用时释放条件锁并持有进程锁，确保唤醒操作的原子性。

  ```c
  sleep(void *chan, struct spinlock *lk) {
    acquire(&p->lock);
    release(lk); // 释放条件锁
    p->chan = chan;
    p->state = SLEEPING;
    sched(); // 切换上下文
    release(&p->lock);
  }
  ```

#### **其他系统的优化方案**

1. **FreeBSD 的 `msleep`**：类似 xv6，使用显式锁保护睡眠条件。
2. **Plan 9 的回调函数**：在持有调度锁时进行最后条件检查，避免唤醒丢失。
3. **Linux 的等待队列（Wait Queue）**：使用队列结构管理等待进程，队列自带锁，提升效率。
4. **条件变量（Condition Variable）**：线程库中的抽象，提供 `wait` 和 `signal`/`broadcast` 操作，避免惊群效应。
   - **`signal`**：唤醒单个等待进程。
   - **`broadcast`**：唤醒所有等待进程。

------

### **3. 唤醒（`wakeup`）的效率优化**

#### **问题：全进程扫描的低效性**

- xv6 的 `wakeup` 遍历所有进程检查等待通道，时间复杂度为 O(n)。
- **改进方案**：使用等待队列（如 Linux）或集合（如 Plan 9 的 Rendez）直接管理等待进程，时间复杂度降至 O(1)。

#### **惊群效应（Thundering Herd）**

- **现象**：`wakeup` 唤醒所有等待同一通道的进程，引发资源竞争。
- **解决**：通过 `signal` 仅唤醒一个进程，或结合条件判断减少无效唤醒。

------

### **4. 信号量（Semaphore）的同步优势**

- **显式计数**：直接记录资源可用数（如管道缓冲区字节数或僵尸进程数）。
- **避免典型问题**：
  - **丢失唤醒**：计数跟踪已发生的唤醒次数。
  - **虚假唤醒**：计数非零时才允许消费。
  - **惊群效应**：仅唤醒必要进程。

------

### **5. 进程终止与清理的复杂性**

#### **挑战**

- **深层睡眠**：被终止进程可能处于内核深层调用栈，需谨慎展开以避免资源泄漏。
- **信号中断**：Unix 中信号可中断睡眠，系统调用返回 `EINTR`，xv6 未实现此机制。
- **C 语言的限制**：缺乏异常处理，需手动清理资源。

#### **xv6 的 `kill` 机制缺陷**

1. **未检查 `p->killed` 的睡眠循环**：
   - 部分代码（如磁盘 I/O）未在循环中检查终止标志，导致延迟退出。
2. **竞争条件**：
   - `kill` 可能在条件检查后、`sleep` 前设置 `p->killed`，导致进程未及时终止。
   - **示例**：等待控制台输入的进程若未收到输入，可能永远无法检测到终止标志。

------

### **6. 进程管理优化**

#### **进程槽分配**

- **xv6 的线性扫描**：`allocproc` 遍历 `proc[]` 数组查找空闲槽，时间复杂度 O(n)。
- **实际系统优化**：使用空闲链表（Free List）实现 O(1) 分配。

------

### **7. 对比其他操作系统的同步机制**

| **机制**           | **实现特点**                                | **优势**                       | **劣势**         |
| :----------------- | :------------------------------------------ | :----------------------------- | :--------------- |
| **xv6 sleep**      | 显式锁保护条件，遍历进程表唤醒              | 简单，适合教学                 | 效率低，惊群效应 |
| **Linux 等待队列** | 内部锁管理等待队列，支持高效唤醒            | 高效，减少扫描开销             | 实现复杂         |
| **条件变量**       | `wait` 和 `signal`/`broadcast` 分离唤醒操作 | 灵活，避免惊群效应             | 需配合互斥锁使用 |
| **信号量**         | 显式计数跟踪资源，原子操作                  | 避免丢失唤醒，天然支持资源计数 | 需额外结构管理   |

------

### **总结**

xv6 的设计以简洁性为核心，其调度、同步和进程管理机制体现了操作系统的基础原理，但也存在效率与功能上的局限：

- **轮转调度**：简单但缺乏优先级支持，适用于低复杂度场景。
- **`sleep`/`wakeup`**：通过锁解决多核竞争，但唤醒效率低下。
- **进程终止**：依赖协作式检查，存在竞争和延迟问题。
- **改进方向**：引入等待队列、条件变量和信号量等高级抽象，优化资源管理策略。

实际操作系统需在公平性、吞吐量、实时性之间权衡，通过更精细的机制（如优先级继承、中断处理）应对复杂场景，而 xv6 为理解这些机制提供了坚实基础。

## exersice

1. Sleep has to check lk != &p->lock to avoid a deadlock Suppose the special case were eliminated by replacing if(lk != &p->lock){ acquire(&p->lock); release(lk); } with release(lk); acquire(&p->lock); Doing this would break sleep. How? 
2.  Implement semaphores in xv6 without using sleep and wakeup (but it is OK to use spin locks). Replace the uses of sleep and wakeup in xv6 with semaphores. Judge the result. 
3. Fix the race mentioned above between kill and sleep, so that a kill that occurs after the victim’s sleep loop checks p->killed but before it calls sleep results in the victim abandoning the current system call. 
4.  Design a plan so that every sleep loop checks p->killed so that, for example, a process that is in the virtio driver can return quickly from the while loop if it is killed by another process. 
5.  Modify xv6 to use only one context switch when switching from one process’s kernel thread to another, rather than switching through the scheduler thread. The yielding thread will need to select the next thread itself and call swtch. The challenges will be to prevent multiple cores from executing the same thread accidentally; to get the locking right; and to avoid deadlocks. 
6. Modify xv6’s scheduler to use the RISC-V WFI (wait for interrupt) instruction when no processes are runnable. Try to ensure that, any time there are runnable processes waiting to run, no cores are pausing in WFI
