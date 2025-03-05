#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    // 检查命令行参数的数量是否少于2个
    if (argc < 2) {
        // 如果参数数量不足，打印使用说明并退出程序
        printf("usage: sleep <ticks>\n");
        exit(1); // 使用非零值表示异常退出
    }
    
    // 将第一个命令行参数转换为整数，并调用sleep函数使程序暂停指定的时间（以秒为单位）
    sleep(atoi(argv[1]));
    
    // 正常退出程序
    exit(0);
}