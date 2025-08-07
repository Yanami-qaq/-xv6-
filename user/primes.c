#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 素数筛法进程函数
// 每个进程负责筛选出一个素数，并过滤掉该素数的所有倍数
void
sieve(int p[2])
{
  int prime;  // 当前进程找到的素数
  int n;      // 从管道读取的数字
  
  // 关闭写端，因为当前进程只需要读取
  close(p[1]);
  
  // 读取第一个数字作为素数
  if(read(p[0], &prime, sizeof(prime)) != sizeof(prime)){
    // 如果没有数字可读，说明已经结束
    close(p[0]);
    exit(0);
  }
  
  // 输出找到的素数
  printf("prime %d\n", prime);
  
  // 创建新的管道用于传递给下一个进程
  int new_pipe[2];
  if(pipe(new_pipe) < 0){
    fprintf(2, "primes: pipe creation failed\n");
    exit(1);
  }
  
  // 创建子进程继续筛选
  int pid = fork();
  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    exit(1);
  } else if(pid == 0){
    // 子进程：递归调用sieve函数继续筛选
    sieve(new_pipe);
  } else {
    // 父进程：过滤当前素数的倍数
    close(new_pipe[0]);  // 关闭读端，只需要写
    
    // 读取剩余的数字，过滤掉当前素数的倍数
    while(read(p[0], &n, sizeof(n)) == sizeof(n)){
      // 如果数字不能被当前素数整除，则传递给下一个进程
      if(n % prime != 0){
        if(write(new_pipe[1], &n, sizeof(n)) != sizeof(n)){
          fprintf(2, "primes: write failed\n");
          exit(1);
        }
      }
    }
    
    // 关闭管道
    close(p[0]);
    close(new_pipe[1]);
    
    // 等待子进程结束
    wait(0);
    exit(0);
  }
}

int
main(int argc, char *argv[])
{
  // 检查命令行参数，primes程序不需要任何参数
  if(argc != 1){
    fprintf(2, "Usage: primes\n");
    exit(1);
  }
  
  // 创建初始管道
  int p[2];
  if(pipe(p) < 0){
    fprintf(2, "primes: pipe creation failed\n");
    exit(1);
  }
  
  // 创建子进程开始素数筛法
  int pid = fork();
  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    exit(1);
  } else if(pid == 0){
    // 子进程：开始素数筛法
    sieve(p);
  } else {
    // 父进程：向管道写入2到35的数字
    close(p[0]);  // 关闭读端，只需要写
    
    // 向管道写入2到35的数字
    for(int i = 2; i <= 35; i++){
      if(write(p[1], &i, sizeof(i)) != sizeof(i)){
        fprintf(2, "primes: write failed\n");
        exit(1);
      }
    }
    
    // 关闭写端，表示没有更多数据
    close(p[1]);
    
    // 等待子进程结束
    wait(0);
    exit(0);
  }
  
  // 确保所有分支都有返回值
  return 0;
}
