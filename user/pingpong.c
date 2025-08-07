#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  // 检查命令行参数，pingpong程序不需要任何参数
  if(argc != 1){
    fprintf(2, "Usage: pingpong\n");
    exit(1);
  }

  // 定义两个管道数组
  // pipe1: 父进程 -> 子进程 (ping)
  // pipe2: 子进程 -> 父进程 (pong)
  int pipe1[2];  // pipe1[0]是读端，pipe1[1]是写端
  int pipe2[2];  // pipe2[0]是读端，pipe2[1]是写端
  
  // 创建第一个管道（父进程到子进程）
  if(pipe(pipe1) < 0){
    fprintf(2, "pingpong: pipe1 creation failed\n");
    exit(1);
  }
  
  // 创建第二个管道（子进程到父进程）
  if(pipe(pipe2) < 0){
    fprintf(2, "pingpong: pipe2 creation failed\n");
    exit(1);
  }

  // 使用fork()创建子进程
  int pid = fork();
  
  if(pid < 0){
    // fork失败
    fprintf(2, "pingpong: fork failed\n");
    exit(1);
  } else if(pid == 0){
    // 子进程代码
    // 关闭不需要的管道端
    close(pipe1[1]);  // 关闭pipe1的写端（子进程不需要向pipe1写）
    close(pipe2[0]);  // 关闭pipe2的读端（子进程不需要从pipe2读）
    
    // 从pipe1读取父进程发送的字节
    char buf;
    if(read(pipe1[0], &buf, 1) != 1){
      fprintf(2, "pingpong: child read failed\n");
      exit(1);
    }
    
    // 打印接收到的ping消息
    // getpid()返回当前进程的PID
    printf("%d: received ping\n", getpid());
    
    // 将字节写回pipe2给父进程
    if(write(pipe2[1], &buf, 1) != 1){
      fprintf(2, "pingpong: child write failed\n");
      exit(1);
    }
    
    // 关闭管道并退出子进程
    close(pipe1[0]);
    close(pipe2[1]);
    exit(0);
    
  } else {
    // 父进程代码
    // 关闭不需要的管道端
    close(pipe1[0]);  // 关闭pipe1的读端（父进程不需要从pipe1读）
    close(pipe2[1]);  // 关闭pipe2的写端（父进程不需要向pipe2写）
    
    // 向pipe1发送一个字节给子进程
    char ping_byte = 'P';  // 发送字符'P'作为ping信号
    if(write(pipe1[1], &ping_byte, 1) != 1){
      fprintf(2, "pingpong: parent write failed\n");
      exit(1);
    }
    
    // 从pipe2读取子进程发送回的字节
    char buf;
    if(read(pipe2[0], &buf, 1) != 1){
      fprintf(2, "pingpong: parent read failed\n");
      exit(1);
    }
    
    // 打印接收到的pong消息
    printf("%d: received pong\n", getpid());
    
    // 等待子进程结束
    wait(0);
    
    // 关闭管道并退出父进程
    close(pipe1[1]);
    close(pipe2[0]);
    exit(0);
  }
}
