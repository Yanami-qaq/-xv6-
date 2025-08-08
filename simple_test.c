#include "kernel/types.h"
#include "kernel/memlayout.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  printf("开始简单COW测试...\n");
  
  // 分配一些内存
  char *p = sbrk(4096);
  if(p == (char*)0xffffffffffffffffL){
    printf("sbrk failed\n");
    exit(1);
  }
  
  // 写入数据
  for(int i = 0; i < 4096; i++){
    p[i] = i % 256;
  }
  
  printf("内存分配和写入成功\n");
  
  // fork一个子进程
  int pid = fork();
  if(pid < 0){
    printf("fork failed\n");
    exit(1);
  }
  
  if(pid == 0){
    // 子进程
    printf("子进程: 修改内存\n");
    for(int i = 0; i < 4096; i++){
      p[i] = (i % 256) + 100;  // 修改数据
    }
    printf("子进程: 内存修改完成\n");
    exit(0);
  }
  
  // 父进程等待子进程
  wait(0);
  
  // 检查父进程的内存是否没有被修改
  printf("父进程: 检查内存\n");
  for(int i = 0; i < 4096; i++){
    if(p[i] != (i % 256)){
      printf("错误: 父进程内存被修改了\n");
      exit(1);
    }
  }
  
  printf("父进程: 内存检查通过\n");
  
  // 释放内存
  if(sbrk(-4096) == (char*)0xffffffffffffffffL){
    printf("sbrk(-4096) failed\n");
    exit(1);
  }
  
  printf("简单COW测试通过!\n");
  exit(0);
}
