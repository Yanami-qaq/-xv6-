#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  // 检查命令行参数数量
  // sleep程序需要恰好一个参数：睡眠的tick数
  if(argc != 2){
    // 如果参数数量不正确，打印使用说明并退出
    // fprintf(2, ...) 将错误信息输出到标准错误流（文件描述符2）
    fprintf(2, "Usage: sleep <ticks>\n");
    exit(1);  // 返回错误状态码1
  }

  // 将命令行参数从字符串转换为整数
  // argv[1] 是用户输入的tick数（字符串格式）
  // atoi() 函数将字符串转换为整数
  int ticks = atoi(argv[1]);
  
  // 验证转换后的数值是否有效
  // 检查条件：如果ticks <= 0 且 原字符串不是"0"
  // 这样可以捕获负数、非数字字符串等无效输入
  if(ticks <= 0 && strcmp(argv[1], "0") != 0){
    fprintf(2, "Error: Invalid number of ticks\n");
    exit(1);
  }

  // 调用系统调用sleep()来暂停指定的tick数
  // sleep() 函数在 user.h 中声明，由xv6内核提供
  // tick是xv6内核定义的时间单位，由定时器芯片的中断间隔决定
  sleep(ticks);
  
  // 睡眠完成后正常退出，返回状态码0表示成功
  exit(0);
}
