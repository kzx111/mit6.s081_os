#include "kernel/types.h"
#include "user/user.h"
#define READEND 0
#define WRITEEND 1
#define NUM 35
//注意当管道的写入端关闭时，read函数返回0。
// 注意关闭进程不需要的文件描述符，否则程序将在第一个进程到达35之前耗尽xv6资源。
void child(int* pl){
    int prime;
    if(read(pl[READEND],& prime,sizeof(int))==0) exit(0);
    printf("prime %d\n",prime);
    int pr[2];
    pipe(pr);
    if(fork()){
        int t;
        close(pr[READEND]);
        while(read(pl[READEND],& t,sizeof(int))){
            if(t%prime!=0) write(pr[WRITEEND],&t,sizeof(int));
        }
        close(pr[WRITEEND]);
        close(pl[READEND]);
        wait(0);
        exit(0);
    }
    else{
        close(pl[READEND]);
        close(pr[WRITEEND]);
        child(pr);
        close(pr[READEND]);
        exit(0);
    }
}

int main(int argc,char argv[]){
    int pl[2];
    pipe(pl);
    if(fork()){//parent
        close(pl[READEND]);
        for(int i=2;i<=NUM;i++){
            write(pl[WRITEEND],& i,sizeof(int));
        }
        close(pl[WRITEEND]);
        wait(0);//等待子进程完成
    }
    else{
        close(pl[WRITEEND]);
        child(pl);
        close(pl[READEND]);
    }


    exit(0);



}