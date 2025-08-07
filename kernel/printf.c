//
// formatted console output -- printf, panic.
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

volatile int panicked = 0;

// lock to avoid interleaving concurrent printf's.
static struct {
  struct spinlock lock;
  int locking;
} pr;

static char digits[] = "0123456789abcdef";

static void
printint(int xx, int base, int sign)
{
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}

static void
printptr(uint64 x)
{
  int i;
  consputc('0');
  consputc('x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console. only understands %d, %x, %p, %s.
void
printf(char *fmt, ...)
{
  va_list ap;
  int i, c, locking;
  char *s;

  locking = pr.locking;
  if(locking)
    acquire(&pr.lock);

  if (fmt == 0)
    panic("null fmt");

  va_start(ap, fmt);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      printptr(va_arg(ap, uint64));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&pr.lock);
}

void
panic(char *s)
{
  pr.locking = 0;
  printf("panic: ");
  printf(s);
  printf("\n");
  
  // 在panic时打印backtrace
  backtrace();
  
  panicked = 1; // freeze uart output from other CPUs
  for(;;)
    ;
}

void
printfinit(void)
{
  initlock(&pr.lock, "pr");
  pr.locking = 1;
}

// 实现backtrace函数，打印调用栈
void
backtrace(void)
{
  printf("backtrace:\n");
  
  // 获取当前帧指针
  uint64 fp = r_fp();
  
  // 遍历调用栈，最多打印10层
  int depth = 0;
  while(fp != 0 && depth < 10) {
    // 计算栈页的边界
    uint64 stack_top = PGROUNDUP(fp);
    uint64 stack_bottom = PGROUNDDOWN(fp);
    
    // 检查帧指针是否在有效的栈页内
    if(fp < stack_bottom || fp >= stack_top) {
      break;
    }
    
    // 获取返回地址（帧指针-8的位置）
    uint64 ra = *(uint64*)(fp - 8);
    
    // 检查返回地址是否在合理的内核地址范围内（0x80000000到0x80000000+2MB）
    if(ra < 0x80000000 || ra > 0x80000000 + 0x200000) {
      break;
    }
    
    // 打印返回地址（使用%p格式符来正确显示64位地址，格式为0x000000008000xxxx）
    printf("%p\n", ra);
    
    // 获取上一个帧指针（帧指针-16的位置）
    uint64 prev_fp = *(uint64*)(fp - 16);
    
    // 检查上一个帧指针是否有效
    if(prev_fp <= fp || prev_fp >= stack_top || prev_fp == 0) {
      break;
    }
    
    fp = prev_fp;
    depth++;
  }
}
