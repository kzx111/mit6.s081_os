# [lec16-file-system-performance-and-fast-crash-recovery-robert](https://github.com/huihongxiao/MIT6.S081/tree/master/lec16-file-system-performance-and-fast-crash-recovery-robert)

* logging 重要规则
  * write ahead rule(预写规则): 一系列写操作具有原子性, 系统需要先将写操作记录到日志中, 再将这些写操作应用到文件系统的实际位置(在做任何实际修改之前将所有更新提交到日志).
  * freeing rule(释放规则): 直到日志中所有写操作更新到文件系统之前, 都不能释放或者重用日志. 即在日志中删除一个事务之前, 必须将日志中的所有 block 都写到文件系统中.
    