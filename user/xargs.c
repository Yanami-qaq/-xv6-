#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 从标准输入读取一行
// 返回读取的字符数，不包括换行符
int
readline(char *buf, int max)
{
  int i = 0;
  char c;
  
  // 逐字符读取，直到遇到换行符或达到最大长度
  while(i < max - 1){
    if(read(0, &c, 1) != 1){
      // 读取失败或到达文件末尾
      break;
    }
    if(c == '\n'){
      // 遇到换行符，结束读取
      break;
    }
    buf[i++] = c;
  }
  
  // 添加字符串结束符
  buf[i] = '\0';
  return i;
}

// 执行命令函数
// argv: 命令及其参数数组
// argc: 参数数量
// extra_arg: 从标准输入读取的额外参数
void
run_command(char *argv[], int argc, char *extra_arg)
{
  // 创建新的参数数组，包含原有参数和额外参数
  char *new_argv[32];  // 假设最多32个参数
  int new_argc = 0;
  
  // 复制原有参数
  for(int i = 0; i < argc; i++){
    new_argv[new_argc++] = argv[i];
  }
  
  // 添加额外参数
  if(extra_arg[0] != '\0'){  // 只有当额外参数不为空时才添加
    new_argv[new_argc++] = extra_arg;
  }
  
  // 添加NULL结束符
  new_argv[new_argc] = 0;
  
  // 创建子进程执行命令
  int pid = fork();
  if(pid < 0){
    fprintf(2, "xargs: fork failed\n");
    exit(1);
  } else if(pid == 0){
    // 子进程：执行命令
    exec(new_argv[0], new_argv);
    // 如果exec成功，不会执行到这里
    fprintf(2, "xargs: exec %s failed\n", new_argv[0]);
    exit(1);
  } else {
    // 父进程：等待子进程结束
    wait(0);
  }
}

int
main(int argc, char *argv[])
{
  // 检查命令行参数
  // xargs程序需要至少一个参数：要执行的命令
  if(argc < 2){
    fprintf(2, "Usage: xargs <command> [args...]\n");
    exit(1);
  }
  
  char line[512];  // 存储从标准输入读取的行
  
  // 从标准输入逐行读取
  while(readline(line, sizeof(line)) > 0){
    // 为每一行执行指定的命令
    // argv[1]是命令名，argv[2]开始是命令的参数
    // line是从标准输入读取的额外参数
    run_command(&argv[1], argc - 1, line);
  }
  
  exit(0);
}
