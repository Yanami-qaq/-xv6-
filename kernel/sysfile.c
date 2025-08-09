//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "memlayout.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// mmap system call - lab 10
uint64
sys_mmap(void)
{
  uint64 addr;
  int len, prot, flags, fd, offset;
  struct file *f;
  struct proc *p = myproc();
  
  // Get arguments
  if(argaddr(0, &addr) < 0 || argint(1, &len) < 0 || argint(2, &prot) < 0 ||
     argint(3, &flags) < 0 || argint(4, &fd) < 0 || argint(5, &offset) < 0)
    return -1;
  
  // Check parameters
  if(len <= 0 || offset < 0)
    return -1;
  
  // Check flags
  if(flags != MAP_SHARED && flags != MAP_PRIVATE)
    return -1;
  
  // Get file descriptor
  if(fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0)
    return -1;
  
  // Check permissions: if requesting write access for MAP_SHARED, file must be opened for writing
  if((prot & PROT_WRITE) && (flags & MAP_SHARED) && !(f->type == FD_INODE && f->writable))
    return -1;
  
  // Find available VMA
  int i;
  for(i = 0; i < NVMA; i++) {
    if(p->vma[i].addr == 0)
      break;
  }
  if(i == NVMA)
    return -1;
  
  // Calculate map address
  uint64 map_addr = MMAPMINADDR;
  for(int j = 0; j < NVMA; j++) {
    if(p->vma[j].addr != 0) {
      uint64 end = p->vma[j].addr + p->vma[j].len;
      if(end > map_addr)
        map_addr = end;
    }
  }
  map_addr = PGROUNDUP(map_addr);
  
  // Check if mapping would overlap with TRAPFRAME
  if(map_addr + len > TRAPFRAME)
    return -1;
  
  // Set VMA
  p->vma[i].addr = map_addr;
  p->vma[i].len = len;
  p->vma[i].prot = prot;
  p->vma[i].flags = flags;
  p->vma[i].offset = offset;
  p->vma[i].f = filedup(f);  // Increase reference count
  
  return map_addr;
}

// munmap system call - lab 10
uint64
sys_munmap(void)
{
  uint64 addr;
  int len;
  struct proc *p = myproc();
  
  // Get arguments
  if(argaddr(0, &addr) < 0 || argint(1, &len) < 0)
    return -1;
  
  // Check parameters
  if(len <= 0)
    return -1;
  
  // Find corresponding VMA
  int i;
  for(i = 0; i < NVMA; i++) {
    if(i < NVMA && p->vma[i].addr != 0 && addr >= p->vma[i].addr && 
       addr < p->vma[i].addr + p->vma[i].len)
      break;
  }
  if(i == NVMA)
    return -1;
  
  struct vm_area *vma = &p->vma[i];
  
  // If MAP_SHARED, write back dirty pages
  if(vma->flags & MAP_SHARED && vma->f != 0) {
    for(uint64 va = addr; va < addr + len; va += PGSIZE) {
      pte_t *pte = walk(p->pagetable, va, 0);
      if(pte && (*pte & PTE_V)) {
        // Page is mapped, check if dirty
        if(uvmgetdirty(p->pagetable, va)) {
          // Write back dirty page
          char *pa = (char *)PTE2PA(*pte);
          begin_op();
          ilock(vma->f->ip);
          writei(vma->f->ip, 0, (uint64)pa, va - vma->addr + vma->offset, PGSIZE);
          iunlock(vma->f->ip);
          end_op();
        }
      }
    }
  }
  
  // Unmap pages that are actually mapped
  for(uint64 va = addr; va < addr + len; va += PGSIZE) {
    pte_t *pte = walk(p->pagetable, va, 0);
    if(pte && (*pte & PTE_V)) {
      // Page is mapped, unmap it
      uint64 pa = PTE2PA(*pte);
      *pte = 0;
      kfree((void*)pa);
    }
  }
  
  // Update VMA
  if(addr == vma->addr && len >= vma->len) {
    // Unmapping entire region
    if(vma->f != 0) {
      fileclose(vma->f);
      vma->f = 0;
    }
    vma->addr = 0;
    vma->len = 0;
    vma->prot = 0;
    vma->flags = 0;
    vma->offset = 0;
  } else if(addr == vma->addr) {
    // Unmapping from beginning
    vma->addr += len;
    vma->offset += len;
    vma->len -= len;
  } else if(addr + len == vma->addr + vma->len) {
    // Unmapping from end
    vma->len -= len;
  }
  
  return 0;
}

// Handle mmap page fault - lab 10
int
handle_mmap_fault(uint64 va)
{
  struct proc *p = myproc();
  if(p == 0)
    return -1;
  
  // Find corresponding VMA
  int i;
  for(i = 0; i < NVMA; i++) {
    if(p->vma[i].addr != 0 && va >= p->vma[i].addr && 
       va < p->vma[i].addr + p->vma[i].len)
      break;
  }
  if(i == NVMA)
    return -1;
  
  struct vm_area *vma = &p->vma[i];
  
  // Calculate page offset
  uint64 page_offset = va - vma->addr;
  uint64 file_offset = vma->offset + page_offset;
  
  // Allocate physical page
  char *pa = kalloc();
  if(pa == 0)
    return -1;
  
  // Clear the page
  memset(pa, 0, PGSIZE);
  
  // Read file content if within file bounds
  if(vma->f != 0 && file_offset < vma->f->ip->size) {
    uint64 bytes_to_read = PGSIZE;
    if(file_offset + bytes_to_read > vma->f->ip->size)
      bytes_to_read = vma->f->ip->size - file_offset;
    
    if(bytes_to_read > 0) {
      begin_op();
      ilock(vma->f->ip);
      readi(vma->f->ip, 0, (uint64)pa, file_offset, bytes_to_read);
      iunlock(vma->f->ip);
      end_op();
    }
  }
  
  // Map the page
  int flags = PTE_U;
  if(vma->prot & PROT_READ)
    flags |= PTE_R;
  if(vma->prot & PROT_WRITE)
    flags |= PTE_W;
  if(vma->prot & PROT_EXEC)
    flags |= PTE_X;
  
  if(mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)pa, flags) != 0) {
    kfree(pa);
    return -1;
  }
  
  return 0;
}
