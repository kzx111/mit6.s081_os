# c语言C++使用的时候遇到的语法问题

1.  [acwing](https://www.acwing.com/problem/content/description/3598/)

```c
    int n, t;
    int* root = 0;
    scanf_s("%d",&n);
    for (int i = 0; i < n; i++) {
        scanf_s("%d", &t);
        *root = 0;
    }

    return 0;
```

这段代码错误原因是，Segmentation Fault ，因为root指针是一个空指针，不能复制

所以需要另一个值，并将将该值的地址赋值给该指针。

```c
    int n, t;
    int root1=0;
    int *root =&root1; 
    scanf_s("%d",&n);
    for (int i = 0; i < n; i++) {
        scanf_s("%d", &t);
        *root = 0;
    }

    return 0;
```

指针需要初始化，不然就是野指针。野指针具体参考：[野指针](https://blog.csdn.net/fancynthia/article/details/122750831)

[悬空指针](https://blog.csdn.net/nyist_zxp/article/details/119478944)

[解释为什么NULL指针不能赋值](https://blog.csdn.net/slty_123/article/details/144838619)

2. 双重指针

xv6中，系统调用sys_pipe调用了pipealloc函数，里面需要修改指针的内容所以要双重指针。

```c
uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
   ...
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  ...... 
   
}


int
pipealloc(struct file **f0, struct file **f1)
{
  struct pipe *pi;

  pi = 0;
  *f0 = *f1 = 0;
  if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
    goto bad;
  ......  
  
  (*f0)->type = FD_PIPE;
  (*f0)->readable = 1;
  (*f0)->writable = 0;
  (*f0)->pipe = pi;
  (*f1)->type = FD_PIPE;
  (*f1)->readable = 0;
  (*f1)->writable = 1;
  (*f1)->pipe = pi;
  return 0;
  ....
}
```

3. 关于lru算法实现

2025年4月15日，我在leetcode中实现lru算法，并用该算法来证明2025年408真题第26道选择题。

[lru算法实现](https://leetcode.cn/problems/lru-cache/description/?envType=problem-list-v2&envId=IfI25erU)

基本逻辑我倒是对了，可是一直报错，最后不得已用vs来调试，发现vs和leetcode里面的编译器都不会给你初始化，而是给变量一个随机的值，所以一直报错，而acwing里面就是会给你初始化为0.这里的初始化是指hashtable的初始化。

在这个题中，我对链表操作更加清晰明了，对链表的插入，删除，这里也有相关特殊情况，这个题目中我用的是双链表，不过，我这里需要对头节点插入和删除要特殊处理，其实有一个方法可以让其统一化，我想了想，也就第一个节点，之前是null的话，我们添加一个头节点，让头节点啥数据都没有，还有最后一个节点的后一位是null，我们添加一个尾节点，与头节点一样的作用，为了让对节点操作统一化。







4. 关于xv6工程内的问题

​			2025年4月17日，一个是usys.pl文件不能加注释，另一个是static 函数只能用于本文件中，关于c语言代码进程的结构，sh函数还有内核怎么分布。



5. 还是xv6的问题

​		2025年4月20号，这个主要是sys_mmap函数需要我们自己实现的min和max函数，而我一开始函数参数和返回数都是int而原参数为uint64，这样会导致负数然后变为非常大的数。
