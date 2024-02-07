/*
 * Copyright (C) 2006  Insigma Co., Ltd
 *
 * This software has been developed while working on the Linux Unified Kernel
 * Project (http://www.longene.org) in the Insigma Research Institute,  
 * which is a subdivision of Insigma Co., Ltd (http://www.insigma.com.cn).
 * 
 * The project is sponsored by Insigma Co., Ltd.
 *
 * The authors can be reached at linux@insigma.com.cn.
 */

#ifndef _UK_LIB_H
#define _UK_LIB_H

#include <linux/types.h>
#include <linux/major.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <asm/div64.h>

#include "stdarg.h"
#include "klog.h"
#include "sys/poll.h"
#include "sys/epoll.h"
#include "sys/socket.h"
#include "sys/ptrace.h"
#include "sys/time.h"
#include "sys/sysctl.h"

/*for major*/
unsigned int gnu_dev_major (unsigned long long int dev);
unsigned int gnu_dev_minor (unsigned long long int dev);
unsigned long long int gnu_dev_makedev (unsigned int major, unsigned int minor);

#define major(dev) gnu_dev_major (dev)
#define minor(dev) gnu_dev_minor (dev)
#define makedev(maj, min) gnu_dev_makedev (maj, min)

typedef unsigned int socklen_t;

typedef unsigned long int __fsblkcnt_t;
#define __fsid_t		struct { int __val[2]; }
struct statfs
  {
    unsigned int f_type;
    unsigned int f_bsize;
    __fsblkcnt_t f_blocks;
    __fsblkcnt_t f_bfree;
    __fsblkcnt_t f_bavail;
    __fsblkcnt_t f_files;
    __fsblkcnt_t f_ffree;
    __fsid_t f_fsid;
    unsigned int f_namelen;
    unsigned int f_spare[6];
  };
  

#define __SI_MAX_SIZE     128
#define __SI_PAD_SIZE     ((__SI_MAX_SIZE / sizeof (int)) - 3)

/*unistd*/

pid_t fork(void);
pid_t setsid(void);


time_t time(time_t *t);
//int gettimeofday(struct timeval *tv, struct timezone *tz);
//int settimeofday(const struct timeval *tv, const struct timezone *tz);
int gettimeofday(void *tv, void *tz);
int settimeofday(const void *tv, const void *tz);
int usleep(unsigned int usec);
//int clock_getres(clockid_t clk_id, struct timespec *res);
//int clock_getres(clockid_t clk_id, struct timespec *res);
//int clock_gettime(clockid_t clk_id, struct timespec *tp);
int clock_settime(clockid_t clk_id, const void *tp);
int clock_gettime(clockid_t clk_id, void *tp);
int clock_settime(clockid_t clk_id, const void *tp);

__sighandler_t  signal(int sig, __sighandler_t func);
int  raise(int sig);
int kill(pid_t pid, int sig);
unsigned int alarm(unsigned int seconds);
//int sigaction(int sig, const struct old_sigaction __user *act, struct old_sigaction __user *oldact);
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
              
int inotify_init(void);
int inotify_add_watch(int fd, const char *pathname, uint32_t mask);
int inotify_rm_watch(int fd, int wd);

pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
int setuid(uid_t uid);

int open(const char *pathname, int flags, ...);
long close(unsigned int fd);

ssize_t read(unsigned int fd, void *buf, size_t size);
ssize_t write(int fd, const void *buf, size_t count);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);

ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset);
ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset);
ssize_t readlink(const char *path, char *buf, size_t bufsiz);

long dup(unsigned int fd);
long dup2(unsigned int oldfd, unsigned int newfd);
int stat(const char *filename, struct stat *stat);
int fstat(unsigned int fd, struct stat *st);
int lstat(const char *path, void *buf);
int symlink(const char *oldpath, const char *newpath);
int rename(const char *oldpath, const char *newpath);
int unlink(const char *pathname);
int truncate(const char *path, off_t length);
int ftruncate(int fd, off_t length);
int statfs(const char *path, struct statfs *buf);
int fstatfs(int fd, struct statfs *buf);
int rmdir(const char *pathname);
int mkdir(const char *pathname, mode_t mode);
int chdir(const char *path);
int fchdir(int fd);
int fsync(int fd);
int mount(char *dev_name, char *dir_name, char *type, unsigned long flags, void *data);
int unmount(char *name, int flags);
int chmod(const char *path, mode_t mode);
int fchmod(int fildes, mode_t mode);
int umask(int mask);
int fcntl(int fd, unsigned int cmd, ... /*unsigned long arg*/);

long socket(int socket_family, int socket_type, int protocol);
long bind(int sockfd, const struct sockaddr *addr, int addrlen);
long listen(int sockfd, int backlog);
long accept(int sockfd, struct sockaddr *addr, int *addrlen);
long connect(int sockfd, const struct sockaddr *addr, int addrlen);
long shutdown(int sockfd, int how);    

long recv(int fd, void *buf, size_t size, unsigned flags);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, int *addrlen);
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, int addrlen);
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);

long socketpair(int family, int type, int protocol, int *sockvec);
long getsockopt(int fd, int level, int optname, void *optval, unsigned int *optlen);
int setsockopt(int sockfd, int level, int optname, const void *optval, int optlen);
int getsockname(int sockfd, struct sockaddr *addr, int * addrlen );
int getpeername(int sockfd, struct sockaddr *addr, int * addrlen );

int epoll_create(int size);
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);

int poll(struct pollfd *pfds, unsigned int nfds, long timeout_msecs);

//int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, void *timeout);

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int munmap(void *addr, size_t length);

int pipe(int pipefd[2]);

pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);


//long ptrace(enum __ptrace_request request, ...);
long ptrace(int request, ...);
long sysconf(int name);

int syscall(int number, ...);

/* sys/resource.h*/
int getrlimit(int resource, struct rlimit *rlim);

int close_fd_by_pid(int fd, pid_t pid);
struct task_struct *uk_find_task_by_pid(pid_t pid);

enum syscalls
{
    UK_exit,
    UK_close,
    UK_open,
    UK_read,
    UK_write,
    UK_dup,
    UK_dup2,
    UK_ioctl,
    UK_newstat,
    UK_newfstat,
    UK_newlstat,
    UK_unlink,
    UK_rename,
    UK_ftruncate,
    UK_fstatfs,
    UK_rmdir,
    UK_mkdir,
    UK_chdir,
    UK_fchdir,
    UK_fsync,
    UK_mount,
    UK_umount,
    UK_chmod,
    UK_fchmod,
    UK_umask,
    UK_fcntl,
    UK_clock_gettime,
    UK_alarm,
    UK_sigaction,
    UK_kill,
    UK_tgkill,
    UK_tkill,
    UK_readlink,
    UK_poll,
    UK_epoll_create,
    UK_epoll_ctl,
    UK_epoll_wait,
    UK_socket,
    UK_accept,
    UK_shutdown,
    UK_recv,
    UK_recvfrom,
    UK_recvmsg,
    UK_send,
    UK_sendto,
    UK_sendmsg,
    UK_socketpair,
    UK_getsockopt,
    UK_setsockopt,
    UK_getsockname,
    UK_getpeername,
    UK_inotify_init,
    UK_inotify_add_watch,
    UK_inotify_rm_watch,
    UK_mmap_pgoff,
    UK_munmap,
    UK_pipe,
    UK_waitpid,
    UK_ptrace,
    UK_getrlimit,
    UK_sched_setaffinity,
    UK_sched_getaffinity,
    UK_NR_SYSCALLS
};

static const char * const syscall_names[UK_NR_SYSCALLS] =
{
    "sys_exit",
    "sys_close",
    "sys_open",
    "sys_read",
    "sys_write",
    "sys_dup",
    "sys_dup2",
    "sys_ioctl",
    "sys_newstat",
    "sys_newfstat",
    "sys_newlstat",
    "sys_unlink",
    "sys_rename",
    "sys_ftruncate",
    "sys_fstatfs",
    "sys_rmdir",
    "sys_mkdir",
    "sys_chdir",
    "sys_fchdir",
    "sys_fsync",
    "sys_mount",
    "sys_umount",
    "sys_chmod",
    "sys_fchmod",
    "sys_umask",
    "sys_fcntl",
    "sys_clock_gettime",
    "sys_alarm",
    "sys_sigaction",
    "sys_kill",
    "sys_tgkill",
    "sys_tkill",
    "sys_readlink",
    "sys_poll",
    "sys_epoll_create",
    "sys_epoll_ctl",
    "sys_epoll_wait",
    "sys_socket",
    "sys_accept",
    "sys_shutdown",
    "sys_recv",
    "sys_recvfrom",
    "sys_recvmsg",
    "sys_send",
    "sys_sendto",
    "sys_sendmsg",
    "sys_socketpair",
    "sys_getsockopt",
    "sys_setsockopt",
    "sys_getsockname",
    "sys_getpeername",
    "sys_inotify_init",
    "sys_inotify_add_watch",
    "sys_inotify_rm_watch",
    "sys_mmap_pgoff",
    "sys_munmap",
    "sys_pipe",
    "sys_waitpid",
    "sys_ptrace",
    "sys_getrlimit",
    "sys_sched_setaffinity",
    "sys_sched_getaffinity",
};

extern void *syscall_array[UK_NR_SYSCALLS];

#define get_syscall(n) syscall_array[n]

extern void init_uk_lock(void);
extern void uk_lock(void);
extern void uk_unlock(void);

typedef struct recursive_spinlock
{
    spinlock_t lock;
    pid_t pid;
    int count;
} recursive_spinlock_t;

#define DEFINE_RECURSIVE_SPINLOCK(name) \
    recursive_spinlock_t name = {.lock=__SPIN_LOCK_UNLOCKED((name).lock), .pid=-1, .count=0,}

void recursive_spinlock_init(recursive_spinlock_t *lock);
void recursive_spin_lock(recursive_spinlock_t *lock);
void recursive_spin_unlock(recursive_spinlock_t *lock);
void recursive_spin_lock_bh(recursive_spinlock_t *lock);
void recursive_spin_unlock_bh(recursive_spinlock_t *lock);

#endif
