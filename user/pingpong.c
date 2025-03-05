#include "kernel/types.h"
#include "user.h"


int main(int argc,char* argv[]){
    //创建两个管道，分别实现ping、pong的读写
      int p[2];//父进程到子进程
      int p2[2];//子进程到父进程
      pipe(p);
      pipe(p2);
      char bur[10];//作为父进程和子进程的读出容器
      if(fork() == 0) { //child read from p ,write to p2
        close(0);
        dup(p[0]);
        close(p[0]);
        close(p[1]);
        close(p2[0]);
        read(0,bur,10);
        printf("%d: received %s",getpid(),bur);
        write(p2[1],"pong",10);
        close(p2[1]);
        exit(0);
    } else {            //parent  write to pipe ,read from p2
         close(0);
         dup(p2[0]);
         close(p2[0]);
         close(p2[1]);
         close(p[0]);
         write(p[1], "ping\n", 10);
         wait(0);          //父进程阻塞，等待子进程读取
         read(0,bur,10);
         printf("%d: received %s\n",getpid(),bur);
         close(p[1]);
         exit(0);
         }

    return 0;
}