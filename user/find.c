#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 递归查找函数
// path: 当前要搜索的路径
// target: 要查找的目标文件名
void
find(char *path, char *target)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  // 打开当前路径
  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  // 获取文件状态信息
  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  // 根据文件类型进行不同处理
  switch(st.type){
  case T_FILE: {
    // 如果是普通文件，检查文件名是否匹配目标
    // 从路径中提取文件名
    char *filename = path;
    for(p = path + strlen(path); p >= path && *p != '/'; p--)
      ;
    filename = p + 1;  // 跳过斜杠，指向文件名
    
    // 比较文件名与目标文件名
    if(strcmp(filename, target) == 0){
      printf("%s\n", path);  // 找到匹配的文件，打印完整路径
    }
    break;
  }

  case T_DIR:
    // 如果是目录，递归搜索其中的所有文件和子目录
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("find: path too long\n");
      break;
    }
    
    // 构建完整路径
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    
    // 读取目录中的每个条目
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;  // 跳过空条目
      
      // 跳过当前目录(.)和父目录(..)，避免无限递归
      if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;
      
      // 构建子项的完整路径
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;  // 确保字符串结束
      
      // 递归搜索子项
      find(buf, target);
    }
    break;
  }
  
  close(fd);
}

int
main(int argc, char *argv[])
{
  // 检查命令行参数
  // find程序需要两个参数：起始目录和目标文件名
  if(argc != 3){
    fprintf(2, "Usage: find <directory> <filename>\n");
    exit(1);
  }
  
  // 开始递归查找
  find(argv[1], argv[2]);
  
  exit(0);
}
