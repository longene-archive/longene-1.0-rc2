/*
 * Server-side file descriptor management
 *
 * Copyright (C) 2000, 2003 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

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

#include "config.h"
#include "wine/port.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef HAVE_POLL_H
#include <poll.h>
#endif
#ifdef HAVE_SYS_POLL_H
#include <sys/poll.h>
#endif
#ifdef HAVE_LINUX_MAJOR_H
#include <linux/major.h>
#endif
#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif
#ifdef HAVE_SYS_VFS_H
/*
 * Solaris defines its system list in sys/list.h.
 * This need to be workaround it here.
 */
#define list SYSLIST
#define list_next SYSLIST_NEXT
#define list_prev SYSLIST_PREV
#define list_head SYSLIST_HEAD
#define list_tail SYSLIST_TAIL
#define list_move_tail SYSLIST_MOVE_TAIL
#define list_remove SYSLIST_REMOVE
#include <sys/vfs.h>
#undef list
#undef list_next
#undef list_prev
#undef list_head
#undef list_tail
#undef list_move_tail
#undef list_remove
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif
#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif
#ifdef HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif
#ifdef HAVE_SYS_EVENT_H
#include <sys/event.h>
#undef LIST_INIT
#undef LIST_ENTRY
#endif
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef HAVE_SYS_SYSCALL_H
#include <sys/syscall.h>
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "object.h"
#include "file.h"
#include "handle.h"
#include "process.h"
#include "request.h"

#include "winternl.h"
#include "winioctl.h"

#ifdef CONFIG_UNIFIED_KERNEL
#include <linux/kthread.h>
#include <linux/poll.h>
#include <linux/version.h>

#define FD_UNINIT 0x1
#define FD_ADDED  0x2
#define FD_REMOVED 0x4

#define DEFAULT_MAP_NUM 16

struct uk_poll_table_entry
{
	struct file *filp;
	unsigned long key;
	wait_queue_t wait;
	wait_queue_head_t *wait_address;
};

struct uk_poll_wqueues
{
    poll_table pt;
    struct uk_poll_table_entry uk_pt_entry;/* only need one. */
    unsigned long _key;
    struct uk_fd *fd;
    int have_inited_flag;/* have run __uk_pollwait to init waitqueue. */
    int	pending_event;
};

struct pid_fd_map
{
    pid_t pid;
    int   unix_fd;
};

extern struct task_struct* timer_kernel_task;
extern void destroy_map_tbl(struct uk_fd *fd);
extern int get_unix_fd_by_pid(struct uk_fd *fd, pid_t pid);
extern int find_unix_fd_by_pid(struct uk_fd* fd, pid_t pid);
extern struct file *get_unix_file( struct uk_fd *fd );

void uk_poll_initwait(struct uk_poll_wqueues *uk_pwq);
void uk_poll_freewait(struct uk_poll_wqueues *uk_pwq);
int uk_add_fd_events(struct uk_fd *fd, struct file *file, int events);
int uk_modify_fd_events(struct uk_fd *fd, struct file *file, int events);
int uk_remove_fd_events(struct uk_poll_wqueues *uk_pwq);
static struct uk_poll_table_entry *uk_poll_get_entry(struct uk_poll_wqueues *uk_pwq);
#endif

#if defined(HAVE_SYS_EPOLL_H) && defined(HAVE_EPOLL_CREATE)
# include <sys/epoll.h>
# define USE_EPOLL
#elif defined(linux) && defined(__i386__) && defined(HAVE_STDINT_H)
# define USE_EPOLL
# define EPOLLIN POLLIN
# define EPOLLOUT POLLOUT
# define EPOLLERR POLLERR
# define EPOLLHUP POLLHUP
# define EPOLL_CTL_ADD 1
# define EPOLL_CTL_DEL 2
# define EPOLL_CTL_MOD 3

typedef union epoll_data
{
  void *ptr;
  int fd;
  uint32_t u32;
  uint64_t u64;
} epoll_data_t;

struct epoll_event
{
  uint32_t events;
  epoll_data_t data;
};

static inline int epoll_create( int size )
{
    return syscall( 254 /*NR_epoll_create*/, size );
}

static inline int epoll_ctl( int epfd, int op, int fd, const struct epoll_event *event )
{
    return syscall( 255 /*NR_epoll_ctl*/, epfd, op, fd, event );
}

static inline int epoll_wait( int epfd, struct epoll_event *events, int maxevents, int timeout )
{
    return syscall( 256 /*NR_epoll_wait*/, epfd, events, maxevents, timeout );
}

#endif /* linux && __i386__ && HAVE_STDINT_H */

#if defined(HAVE_PORT_H) && defined(HAVE_PORT_CREATE)
# include <port.h>
# define USE_EVENT_PORTS
#endif /* HAVE_PORT_H && HAVE_PORT_CREATE */

/* Because of the stupid Posix locking semantics, we need to keep
 * track of all file descriptors referencing a given file, and not
 * close a single one until all the locks are gone (sigh).
 */

/* file descriptor object */

/* closed_fd is used to keep track of the unix fd belonging to a closed fd object */
struct closed_fd
{
    struct list_head entry;       /* entry in inode closed list */
    int         unix_fd;     /* the unix file descriptor */
    char        unlink[1];   /* name to unlink on close (if any) */
};

struct uk_fd
{
    struct object        obj;         /* object header */
    const struct fd_ops *fd_ops;      /* file descriptor operations */
    struct uk_inode        *inode;       /* inode that this fd belongs to */
    struct list_head          inode_entry; /* entry in inode fd list */
    struct closed_fd    *closed;      /* structure to store the unix fd at destroy time */
    struct object       *user;        /* object using this file descriptor */
    struct list_head          locks;       /* list of locks on this fd */
    unsigned int         access;      /* file access (FILE_READ_DATA etc.) */
    unsigned int         options;     /* file options (FILE_DELETE_ON_CLOSE, FILE_SYNCHRONOUS...) */
    unsigned int         sharing;     /* file sharing mode */
    char                *unix_name;   /* unix file name */
    int                  unix_fd;     /* unix file descriptor */
    unsigned int         no_fd_status;/* status to return when unix_fd is -1 */
    unsigned int         cacheable :1;/* can the fd be cached on the client side? */
    unsigned int         signaled :1; /* is the fd signaled? */
    unsigned int         fs_locks :1; /* can we use filesystem locks for this fd? */
    int                  poll_index;  /* index of fd in poll array */
    struct async_queue  *read_q;      /* async readers of this fd */
    struct async_queue  *write_q;     /* async writers of this fd */
    struct async_queue  *wait_q;      /* other async waiters of this fd */
    struct uk_completion   *completion;  /* completion object attached to this fd */
    apc_param_t          comp_key;    /* completion key to set in completion events */
#ifdef CONFIG_UNIFIED_KERNEL
    pid_t creator_pid;
    struct file *unix_file;
    int tbl_index;
    int max_index;
    struct pid_fd_map   *map_tbl;
    struct uk_poll_wqueues	uk_pwq;
    int events;
    atomic_t state;
#endif
};

static void fd_dump( struct object *obj, int verbose );
static void fd_destroy( struct object *obj );

static const struct object_ops fd_ops =
{
    sizeof(struct uk_fd),        /* size */
    fd_dump,                  /* dump */
    no_get_type,              /* get_type */
    no_add_queue,             /* add_queue */
    NULL,                     /* remove_queue */
    NULL,                     /* signaled */
    NULL,                     /* satisfied */
    no_signal,                /* signal */
    no_get_fd,                /* get_fd */
    no_map_access,            /* map_access */
    default_get_sd,           /* get_sd */
    default_set_sd,           /* set_sd */
    no_lookup_name,           /* lookup_name */
    no_open_file,             /* open_file */
    no_close_handle,          /* close_handle */
    fd_destroy                /* destroy */
};

/* device object */

#define DEVICE_HASH_SIZE 7
#define INODE_HASH_SIZE 17

struct device
{
    struct object       obj;        /* object header */
    struct list_head         entry;      /* entry in device hash list */
    dev_t               dev;        /* device number */
    int                 removable;  /* removable device? (or -1 if unknown) */
    struct list_head         inode_hash[INODE_HASH_SIZE];  /* inodes hash table */
};

static void device_dump( struct object *obj, int verbose );
static void device_destroy( struct object *obj );

static const struct object_ops device_ops =
{
    sizeof(struct device),    /* size */
    device_dump,              /* dump */
    no_get_type,              /* get_type */
    no_add_queue,             /* add_queue */
    NULL,                     /* remove_queue */
    NULL,                     /* signaled */
    NULL,                     /* satisfied */
    no_signal,                /* signal */
    no_get_fd,                /* get_fd */
    no_map_access,            /* map_access */
    default_get_sd,           /* get_sd */
    default_set_sd,           /* set_sd */
    no_lookup_name,           /* lookup_name */
    no_open_file,             /* open_file */
    no_close_handle,          /* close_handle */
    device_destroy            /* destroy */
};

/* inode object */

struct uk_inode
{
    struct object       obj;        /* object header */
    struct list_head         entry;      /* inode hash list entry */
    struct device      *device;     /* device containing this inode */
    ino_t               ino;        /* inode number */
    struct list_head         open;       /* list of open file descriptors */
    struct list_head         locks;      /* list of file locks */
    struct list_head         closed;     /* list of file descriptors to close at destroy time */
};

static void inode_dump( struct object *obj, int verbose );
static void inode_destroy( struct object *obj );

static const struct object_ops inode_ops =
{
    sizeof(struct uk_inode),     /* size */
    inode_dump,               /* dump */
    no_get_type,              /* get_type */
    no_add_queue,             /* add_queue */
    NULL,                     /* remove_queue */
    NULL,                     /* signaled */
    NULL,                     /* satisfied */
    no_signal,                /* signal */
    no_get_fd,                /* get_fd */
    no_map_access,            /* map_access */
    default_get_sd,           /* get_sd */
    default_set_sd,           /* set_sd */
    no_lookup_name,           /* lookup_name */
    no_open_file,             /* open_file */
    no_close_handle,          /* close_handle */
    inode_destroy             /* destroy */
};

/* file lock object */

struct uk_file_lock
{
    struct object       obj;         /* object header */
    struct uk_fd          *fd;          /* fd owning this lock */
    struct list_head         fd_entry;    /* entry in list of locks on a given fd */
    struct list_head         inode_entry; /* entry in inode list of locks */
    int                 shared;      /* shared lock? */
    file_pos_t          start;       /* locked region is interval [start;end) */
    file_pos_t          end;
    struct process     *process;     /* process owning this lock */
    struct list_head         proc_entry;  /* entry in list of locks owned by the process */
};

static void file_lock_dump( struct object *obj, int verbose );
static int file_lock_signaled( struct object *obj, struct wait_queue_entry *entry );

static const struct object_ops file_lock_ops =
{
    sizeof(struct uk_file_lock),   /* size */
    file_lock_dump,             /* dump */
    no_get_type,                /* get_type */
    add_queue,                  /* add_queue */
    remove_queue,               /* remove_queue */
    file_lock_signaled,         /* signaled */
    no_satisfied,               /* satisfied */
    no_signal,                  /* signal */
    no_get_fd,                  /* get_fd */
    no_map_access,              /* map_access */
    default_get_sd,             /* get_sd */
    default_set_sd,             /* set_sd */
    no_lookup_name,             /* lookup_name */
    no_open_file,               /* open_file */
    no_close_handle,            /* close_handle */
    no_destroy                  /* destroy */
};


#define OFF_T_MAX       (~((file_pos_t)1 << (8*sizeof(off_t)-1)))
#define FILE_POS_T_MAX  (~(file_pos_t)0)

static file_pos_t max_unix_offset = OFF_T_MAX;

#define DUMP_LONG_LONG(val) do { \
    if (sizeof(val) > sizeof(unsigned long) && (val) > ~0UL) \
        fprintf( stderr, "%lx%08lx", (unsigned long)((unsigned long long)(val) >> 32), (unsigned long)(val) ); \
    else \
        fprintf( stderr, "%lx", (unsigned long)(val) ); \
  } while (0)



/****************************************************************/
/* timeouts support */

struct timeout_user
{
    struct list_head           entry;      /* entry in sorted timeout list */
    timeout_t             when;       /* timeout expiry (absolute time) */
    timeout_callback      callback;   /* callback function */
    void                 *private;    /* callback private data */
};

static struct list_head timeout_list = LIST_INIT(timeout_list);   /* sorted timeouts list */
#ifdef CONFIG_UNIFIED_KERNEL
static inline void set_current_time(void)
{
}
#else
timeout_t current_time;

static inline void set_current_time(void)
{
    static const timeout_t ticks_1601_to_1970 = (timeout_t)86400 * (369 * 365 + 89) * TICKS_PER_SEC;
    struct timeval now;
    gettimeofday( &now, NULL );
    current_time = (timeout_t)now.tv_sec * TICKS_PER_SEC + now.tv_usec * 10 + ticks_1601_to_1970;
}
#endif

#ifdef CONFIG_UNIFIED_KERNEL
extern struct timeout_user *parse_private( timeout_callback func, void *private);
struct timeout_user *alloc_timeout_user(void)
{
    return (struct timeout_user *)mem_alloc( sizeof(struct timeout_user) );
}
#endif

/* add a timeout user */
struct timeout_user *add_timeout_user( timeout_t when, timeout_callback func, void *private )
{
    struct timeout_user *user;
    struct list_head *ptr;

#ifdef CONFIG_UNIFIED_KERNEL
    if (!(user = parse_private(func, private))) /*only for thread_timeout()*/
#endif
    if (!(user = mem_alloc( sizeof(*user) ))) return NULL;
    user->when     = (when > 0) ? when : current_time - when;
    user->callback = func;
    user->private  = private;

    /* Now insert it in the linked list */

    LIST_FOR_EACH( ptr, &timeout_list )
    {
        struct timeout_user *timeout = LIST_ENTRY( ptr, struct timeout_user, entry );
        if (timeout->when >= user->when) break;
    }
    wine_list_add_before( ptr, &user->entry );
#ifdef CONFIG_UNIFIED_KERNEL
    wake_up_process(timer_kernel_task);
#endif
    return user;
}

/* remove a timeout user */
void remove_timeout_user( struct timeout_user *user )
{
    list_remove( &user->entry );
    free( user );
}

/* return a text description of a timeout for debugging purposes */
const char *get_timeout_str( timeout_t timeout )
{
    static char buffer[64];
    long secs, nsecs;

    if (!timeout) return "0";
    if (timeout == TIMEOUT_INFINITE) return "infinite";

    if (timeout < 0)  /* relative */
    {
#ifndef CONFIG_UNIFIED_KERNEL
        secs = -timeout / TICKS_PER_SEC;
        nsecs = -timeout % TICKS_PER_SEC;
#else
        timeout_t t = -timeout;
        nsecs = do_div(t,TICKS_PER_SEC);
        secs = t;
#endif
        sprintf( buffer, "+%ld.%07ld", secs, nsecs );
    }
    else  /* absolute */
    {
#ifndef CONFIG_UNIFIED_KERNEL
        secs = (timeout - current_time) / TICKS_PER_SEC;
        nsecs = (timeout - current_time) % TICKS_PER_SEC;
#else
        timeout_t t = (timeout - current_time);
        nsecs = do_div(t,TICKS_PER_SEC);
        secs = t;
#endif
        if (nsecs < 0)
        {
            nsecs += TICKS_PER_SEC;
            secs--;
        }
        if (secs >= 0)
            sprintf( buffer, "%x%08x (+%ld.%07ld)",
                     (unsigned int)(timeout >> 32), (unsigned int)timeout, secs, nsecs );
        else
            sprintf( buffer, "%x%08x (-%ld.%07ld)",
                     (unsigned int)(timeout >> 32), (unsigned int)timeout,
                     -(secs + 1), TICKS_PER_SEC - nsecs );
    }
    return buffer;
}


/****************************************************************/
/* poll support */

static struct uk_fd **poll_users;              /* users array */
static struct pollfd *pollfd;               /* poll fd array */
static int nb_users;                        /* count of array entries actually in use */
static int active_users;                    /* current_thread number of active users */
static int allocated_users;                 /* count of allocated entries in the array */
static struct uk_fd **freelist;                /* list of free entries in the array */

static int get_next_timeout(void);

#ifdef CONFIG_UNIFIED_KERNEL
static void fd_poll_event( struct uk_fd *fd, int event )
{
    if (fd && fd->fd_ops && fd->fd_ops->poll_event)
    {
        if(fd->uk_pwq.pending_event != 0
                && (fd->uk_pwq.pending_event == event) && event&(POLLERR|POLLHUP))
        {
            /* avoid recursive call the fd_poll_event.*/
            printk("fd_poll_event fd->uk_pwq.pending_event != 0\n");
            return;
        }
        else
        {
            fd->uk_pwq.pending_event = event;
        }

        fd->fd_ops->poll_event(fd, event);

        fd->uk_pwq.pending_event = 0;
    }
}
#else
static inline void fd_poll_event( struct uk_fd *fd, int event )
{
    fd->fd_ops->poll_event( fd, event );
}
#endif

#ifdef USE_EPOLL

static int epoll_fd = -1;

#ifdef CONFIG_UNIFIED_KERNEL

static inline unsigned int uk_do_poll_file(struct file *file, int events, poll_table *pwait)
{
    unsigned int mask;

    mask = DEFAULT_POLLMASK;

    mask = file->f_op->poll(file, pwait);

    /* Mask out unneeded events. */
    mask &= events | POLLERR | POLLHUP;
    return mask;
}

/* old __pollwake use default */
static int __uk_pollwake(wait_queue_t *wait, unsigned mode, int sync, void *key, int mask)
{
    struct file *file;
    int poll_events;
    struct uk_poll_wqueues *uk_pwq = (struct uk_poll_wqueues *)(wait->private);

    /*need to filter the key.*/
    file = get_unix_file(uk_pwq->fd);
    poll_events = file->f_op->poll(file, NULL) & mask;

    /* use fd_poll_event to deal with events.*/
    if(uk_pwq->fd && poll_events)
    {
        fd_poll_event(uk_pwq->fd, poll_events);
    }

    return 1;
}

static int uk_pollwake(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
    struct uk_poll_table_entry *entry;
    int ret;

    entry = container_of(wait, struct uk_poll_table_entry, wait);

    if (key && !((unsigned long)key & entry->key))
        return 0;

    spin_unlock(&entry->wait_address->lock);
    local_irq_enable();

    ret = __uk_pollwake(wait, mode, sync, key, entry->key);

    local_irq_disable();
    spin_lock(&entry->wait_address->lock);

    return ret;
}

/* only one poll_table_entry */
static inline struct uk_poll_table_entry *uk_poll_get_entry(struct uk_poll_wqueues *uk_pwq)
{
    return &uk_pwq->uk_pt_entry;
}

/*
 * if struct file have data,must deal with it.
 * private_data is struct uk_fd
 */
int uk_add_fd_events(struct uk_fd *fd,struct file *file,int events)
{
    struct uk_poll_wqueues *uk_pwq = &fd->uk_pwq;

    if(uk_pwq->have_inited_flag == false)
    {
        poll_table* pt;
        unsigned int mask;

        uk_poll_initwait(uk_pwq);
        uk_pwq->fd = fd;
        uk_pwq->_key = events | POLLERR | POLLHUP;

        atomic_set(&fd->state, FD_ADDED);
        fd->events = events;

        pt = &uk_pwq->pt;
        mask = uk_do_poll_file(file, events, pt);
        if(mask & events)
        {
            fd_poll_event(fd, mask);
        }
    }
    else
    {
        /* have inited. */
        atomic_set(&fd->state, FD_ADDED);
        fd->events = events;
        uk_modify_fd_events(fd, file, events);
    }
    return 0;
}

int uk_remove_fd_events(struct uk_poll_wqueues *uk_pwq)
{
    struct uk_poll_table_entry *entry;

    /* think this carefully,is it necceary?. */
    if(uk_pwq->have_inited_flag == false)
        return 0;

    entry = uk_poll_get_entry(uk_pwq);
    if(entry)
        entry->key = 0;

    return 0;
}

/* change poll fd's events. */
int uk_modify_fd_events(struct uk_fd *fd,struct file *file,int events)
{
    unsigned int mask;
    struct uk_poll_table_entry *entry;
    struct uk_poll_wqueues *uk_pwq = &fd->uk_pwq;

    /* think this carefully,is it necceary?. */
    if(uk_pwq->have_inited_flag == false)
        return 0;

    entry = uk_poll_get_entry(uk_pwq);
    if(entry)
        entry->key = events | POLLERR | POLLHUP;

    if (atomic_read(&fd->state)==FD_ADDED || !fd->events)
    {
        fd->events = events;
    }

    mask = uk_do_poll_file(file, events, NULL);
    if(mask & events)
    {
        fd_poll_event(fd, mask);
    }

    return 0;
}

/* if first call file->f_op->poll,the function will be callback. */
static void __uk_pollwait(struct file *filp, wait_queue_head_t *wait_address,
        poll_table *p)
{
    struct uk_poll_wqueues *uk_pwq;
    struct uk_poll_table_entry *entry;

    uk_pwq = container_of(p, struct uk_poll_wqueues, pt);
    entry = uk_poll_get_entry(uk_pwq);
    if (entry)
    {
        get_file(filp);
        entry->filp = filp;
        entry->wait_address = wait_address;
        entry->key = uk_pwq->_key;
        init_waitqueue_func_entry(&entry->wait, uk_pollwake);
        entry->wait.private = uk_pwq;
        add_wait_queue(wait_address, &entry->wait);
        uk_pwq->have_inited_flag = true;
    }
}

void uk_poll_initwait(struct uk_poll_wqueues *uk_pwq)
{
    init_poll_funcptr(&uk_pwq->pt, __uk_pollwait);
}

/* need to check whether pwd have been initd. */
void uk_poll_freewait(struct uk_poll_wqueues *uk_pwq)
{
    if(uk_pwq && uk_pwq->have_inited_flag)
    {
        struct uk_poll_table_entry *entry = uk_poll_get_entry(uk_pwq);

        /*maybe have error,can not remove wait in callback.*/
        remove_wait_queue(entry->wait_address, &entry->wait);
        fput(entry->filp);
        uk_pwq->have_inited_flag = false;
    }
}

#endif

static inline void init_epoll(void)
{
    epoll_fd = epoll_create( 128 );
}

/* set the events that epoll waits for on this fd; helper for set_fd_events */
static inline void set_fd_epoll_events( struct uk_fd *fd, int user, int events )
{
    struct epoll_event ev;
    int ctl;

    if (epoll_fd == -1) return;

    if (events == -1)  /* stop waiting on this fd completely */
    {
        if (pollfd[user].fd == -1) return;  /* already removed */
        ctl = EPOLL_CTL_DEL;
    }
    else if (pollfd[user].fd == -1)
    {
        if (pollfd[user].events) return;  /* stopped waiting on it, don't restart */
        ctl = EPOLL_CTL_ADD;
    }
    else
    {
        if (pollfd[user].events == events) return;  /* nothing to do */
        ctl = EPOLL_CTL_MOD;
    }

    ev.events = events;
    memset(&ev.data, 0, sizeof(ev.data));
    ev.data.u32 = user;

    if (epoll_ctl( epoll_fd, ctl, fd->unix_fd, &ev ) == -1)
    {
        if (errno == ENOMEM)  /* not enough memory, give up on epoll */
        {
            close( epoll_fd );
            epoll_fd = -1;
        }
        else perror( "epoll_ctl" );  /* should not happen */
    }
}

static inline void remove_epoll_user( struct uk_fd *fd, int user )
{
    if (epoll_fd == -1) return;

    if (pollfd[user].fd != -1)
    {
        struct epoll_event dummy;
        epoll_ctl( epoll_fd, EPOLL_CTL_DEL, get_unix_fd(fd), &dummy );
    }
}

static inline void main_loop_epoll(void)
{
    int i, ret, timeout;
    struct epoll_event events[128];

    assert( POLLIN == EPOLLIN );
    assert( POLLOUT == EPOLLOUT );
    assert( POLLERR == EPOLLERR );
    assert( POLLHUP == EPOLLHUP );

    if (epoll_fd == -1) return;

    while (active_users)
    {
        timeout = get_next_timeout();

        if (!active_users) break;  /* last user removed by a timeout */
        if (epoll_fd == -1) break;  /* an error occurred with epoll */

        ret = epoll_wait( epoll_fd, events, sizeof(events)/sizeof(events[0]), timeout );
        set_current_time();

        /* put the events into the pollfd array first, like poll does */
        for (i = 0; i < ret; i++)
        {
            int user = events[i].data.u32;
            pollfd[user].revents = events[i].events;
        }

        /* read events from the pollfd array, as set_fd_events may modify them */
        for (i = 0; i < ret; i++)
        {
            int user = events[i].data.u32;
            if (pollfd[user].revents) fd_poll_event( poll_users[user], pollfd[user].revents );
        }
    }
}

#elif defined(HAVE_KQUEUE)

static int kqueue_fd = -1;

static inline void init_epoll(void)
{
#ifdef __APPLE__ /* kqueue support is broken in Mac OS < 10.5 */
    int mib[2];
    char release[32];
    size_t len = sizeof(release);

    mib[0] = CTL_KERN;
    mib[1] = KERN_OSRELEASE;
    if (sysctl( mib, 2, release, &len, NULL, 0 ) == -1) return;
    if (atoi(release) < 9) return;
#endif
    kqueue_fd = kqueue();
}

static inline void set_fd_epoll_events( struct uk_fd *fd, int user, int events )
{
    struct kevent ev[2];

    if (kqueue_fd == -1) return;

    EV_SET( &ev[0], fd->unix_fd, EVFILT_READ, 0, NOTE_LOWAT, 1, (void *)user );
    EV_SET( &ev[1], fd->unix_fd, EVFILT_WRITE, 0, NOTE_LOWAT, 1, (void *)user );

    if (events == -1)  /* stop waiting on this fd completely */
    {
        if (pollfd[user].fd == -1) return;  /* already removed */
        ev[0].flags |= EV_DELETE;
        ev[1].flags |= EV_DELETE;
    }
    else if (pollfd[user].fd == -1)
    {
        if (pollfd[user].events) return;  /* stopped waiting on it, don't restart */
        ev[0].flags |= EV_ADD | ((events & POLLIN) ? EV_ENABLE : EV_DISABLE);
        ev[1].flags |= EV_ADD | ((events & POLLOUT) ? EV_ENABLE : EV_DISABLE);
    }
    else
    {
        if (pollfd[user].events == events) return;  /* nothing to do */
        ev[0].flags |= (events & POLLIN) ? EV_ENABLE : EV_DISABLE;
        ev[1].flags |= (events & POLLOUT) ? EV_ENABLE : EV_DISABLE;
    }

    if (kevent( kqueue_fd, ev, 2, NULL, 0, NULL ) == -1)
    {
        if (errno == ENOMEM)  /* not enough memory, give up on kqueue */
        {
            close( kqueue_fd );
            kqueue_fd = -1;
        }
        else perror( "kevent" );  /* should not happen */
    }
}

static inline void remove_epoll_user( struct uk_fd *fd, int user )
{
    if (kqueue_fd == -1) return;

    if (pollfd[user].fd != -1)
    {
        struct kevent ev[2];

        EV_SET( &ev[0], fd->unix_fd, EVFILT_READ, EV_DELETE, 0, 0, 0 );
        EV_SET( &ev[1], fd->unix_fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0 );
        kevent( kqueue_fd, ev, 2, NULL, 0, NULL );
    }
}

static inline void main_loop_epoll(void)
{
    int i, ret, timeout;
    struct kevent events[128];

    if (kqueue_fd == -1) return;

    while (active_users)
    {
        timeout = get_next_timeout();

        if (!active_users) break;  /* last user removed by a timeout */
        if (kqueue_fd == -1) break;  /* an error occurred with kqueue */

        if (timeout != -1)
        {
            struct timespec ts;

            ts.tv_sec = timeout / 1000;
            ts.tv_nsec = (timeout % 1000) * 1000000;
            ret = kevent( kqueue_fd, NULL, 0, events, sizeof(events)/sizeof(events[0]), &ts );
        }
        else ret = kevent( kqueue_fd, NULL, 0, events, sizeof(events)/sizeof(events[0]), NULL );

        set_current_time();

        /* put the events into the pollfd array first, like poll does */
        for (i = 0; i < ret; i++)
        {
            long user = (long)events[i].udata;
            pollfd[user].revents = 0;
        }
        for (i = 0; i < ret; i++)
        {
            long user = (long)events[i].udata;
            if (events[i].filter == EVFILT_READ) pollfd[user].revents |= POLLIN;
            else if (events[i].filter == EVFILT_WRITE) pollfd[user].revents |= POLLOUT;
            if (events[i].flags & EV_EOF) pollfd[user].revents |= POLLHUP;
            if (events[i].flags & EV_ERROR) pollfd[user].revents |= POLLERR;
        }

        /* read events from the pollfd array, as set_fd_events may modify them */
        for (i = 0; i < ret; i++)
        {
            long user = (long)events[i].udata;
            if (pollfd[user].revents) fd_poll_event( poll_users[user], pollfd[user].revents );
            pollfd[user].revents = 0;
        }
    }
}

#elif defined(USE_EVENT_PORTS)

static int port_fd = -1;

static inline void init_epoll(void)
{
    port_fd = port_create();
}

static inline void set_fd_epoll_events( struct uk_fd *fd, int user, int events )
{
    int ret;

    if (port_fd == -1) return;

    if (events == -1)  /* stop waiting on this fd completely */
    {
        if (pollfd[user].fd == -1) return;  /* already removed */
        port_dissociate( port_fd, PORT_SOURCE_FD, fd->unix_fd );
    }
    else if (pollfd[user].fd == -1)
    {
        if (pollfd[user].events) return;  /* stopped waiting on it, don't restart */
        ret = port_associate( port_fd, PORT_SOURCE_FD, fd->unix_fd, events, (void *)user );
    }
    else
    {
        if (pollfd[user].events == events) return;  /* nothing to do */
        ret = port_associate( port_fd, PORT_SOURCE_FD, fd->unix_fd, events, (void *)user );
    }

    if (ret == -1)
    {
        if (errno == ENOMEM)  /* not enough memory, give up on port_associate */
        {
            close( port_fd );
            port_fd = -1;
        }
        else perror( "port_associate" );  /* should not happen */
    }
}

static inline void remove_epoll_user( struct uk_fd *fd, int user )
{
    if (port_fd == -1) return;

    if (pollfd[user].fd != -1)
    {
        port_dissociate( port_fd, PORT_SOURCE_FD, fd->unix_fd );
    }
}

static inline void main_loop_epoll(void)
{
    int i, nget, ret, timeout;
    port_event_t events[128];

    if (port_fd == -1) return;

    while (active_users)
    {
        timeout = get_next_timeout();
        nget = 1;

        if (!active_users) break;  /* last user removed by a timeout */
        if (port_fd == -1) break;  /* an error occurred with event completion */

        if (timeout != -1)
        {
            struct timespec ts;

            ts.tv_sec = timeout / 1000;
            ts.tv_nsec = (timeout % 1000) * 1000000;
            ret = port_getn( port_fd, events, sizeof(events)/sizeof(events[0]), &nget, &ts );
        }
        else ret = port_getn( port_fd, events, sizeof(events)/sizeof(events[0]), &nget, NULL );

	if (ret == -1) break;  /* an error occurred with event completion */

        set_current_time();

        /* put the events into the pollfd array first, like poll does */
        for (i = 0; i < nget; i++)
        {
            long user = (long)events[i].portev_user;
            pollfd[user].revents = events[i].portev_events;
        }

        /* read events from the pollfd array, as set_fd_events may modify them */
        for (i = 0; i < nget; i++)
        {
            long user = (long)events[i].portev_user;
            if (pollfd[user].revents) fd_poll_event( poll_users[user], pollfd[user].revents );
            /* if we are still interested, reassociate the fd */
            if (pollfd[user].fd != -1) {
                port_associate( port_fd, PORT_SOURCE_FD, pollfd[user].fd, pollfd[user].events, (void *)user );
            }
        }
    }
}

#else /* HAVE_KQUEUE */

static inline void init_epoll(void) { }
static inline void set_fd_epoll_events( struct uk_fd *fd, int user, int events ) { }
static inline void remove_epoll_user( struct uk_fd *fd, int user ) { }
static inline void main_loop_epoll(void) { }

#endif /* USE_EPOLL */


/* add a user in the poll array and return its index, or -1 on failure */
static int add_poll_user( struct uk_fd *fd )
{
    int ret;
    if (freelist)
    {
        ret = freelist - poll_users;
        freelist = (struct uk_fd **)poll_users[ret];
    }
    else
    {
        if (nb_users == allocated_users)
        {
            struct uk_fd **newusers;
            struct pollfd *newpoll;
            int new_count = allocated_users ? (allocated_users + allocated_users / 2) : 16;
            if (!(newusers = realloc( poll_users, new_count * sizeof(*poll_users) ))) return -1;
            if (!(newpoll = realloc( pollfd, new_count * sizeof(*pollfd) )))
            {
                if (allocated_users)
                    poll_users = newusers;
                else
                    free( newusers );
                return -1;
            }
            poll_users = newusers;
            pollfd = newpoll;
            if (!allocated_users) init_epoll();
            allocated_users = new_count;
        }
        ret = nb_users++;
    }
    pollfd[ret].fd = -1;
    pollfd[ret].events = 0;
    pollfd[ret].revents = 0;
    poll_users[ret] = fd;
    active_users++;
    return ret;
}

/* remove a user from the poll list */
static void remove_poll_user( struct uk_fd *fd, int user )
{
    assert( user >= 0 );
    assert( poll_users[user] == fd );

    remove_epoll_user( fd, user );
    pollfd[user].fd = -1;
    pollfd[user].events = 0;
    pollfd[user].revents = 0;
    poll_users[user] = (struct uk_fd *)freelist;
    freelist = &poll_users[user];
    active_users--;
}

#ifdef CONFIG_UNIFIED_KERNEL
int timer_loop(void *data)
{
    unsigned int msecs, timeout, next;

    while (1)
    {
        uk_lock();
        next = get_next_timeout();
        uk_unlock();
        if (kthread_should_stop())
        {
            return 0;
        }

        msecs = (next==-1) ? 10000 : next;
        timeout = msecs_to_jiffies(msecs) + 1;

        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(timeout);
    }

    return 0;
}
#endif

/* process pending timeouts and return the time until the next timeout, in milliseconds */
static int get_next_timeout(void)
{
    if (!list_empty( &timeout_list ))
    {
        struct list_head expired_list, *ptr;

        /* first remove all expired timers from the list */

        list_init( &expired_list );
        while ((ptr = list_head( &timeout_list )) != NULL)
        {
            struct timeout_user *timeout = LIST_ENTRY( ptr, struct timeout_user, entry );

            if (timeout->when <= current_time)
            {
                list_remove( &timeout->entry );
                wine_list_add_tail( &expired_list, &timeout->entry );
            }
            else break;
        }

        /* now call the callback for all the removed timers */

        while ((ptr = list_head( &expired_list )) != NULL)
        {
            struct timeout_user *timeout = LIST_ENTRY( ptr, struct timeout_user, entry );
            list_remove( &timeout->entry );
            timeout->callback( timeout->private );
            free( timeout );
        }

        if ((ptr = list_head( &timeout_list )) != NULL)
        {
            struct timeout_user *timeout = LIST_ENTRY( ptr, struct timeout_user, entry );
#ifndef CONFIG_UNIFIED_KERNEL
            int diff = (timeout->when - current_time + 9999) / 10000;
#else
            int diff = 0;
            u64 tmp = (timeout->when - current_time + 9999);
            do_div(tmp, 10000);
            diff = (int)tmp;
#endif
            if (diff < 0) diff = 0;
            return diff;
        }
    }
    return -1;  /* no pending timeouts */
}

/* server main poll() loop */
void main_loop(void)
{
    int i, ret, timeout;

    set_current_time();
    server_start_time = current_time;

    main_loop_epoll();
    /* fall through to normal poll loop */

    while (active_users)
    {
        timeout = get_next_timeout();

        if (!active_users) break;  /* last user removed by a timeout */

        ret = poll( pollfd, nb_users, timeout );
        set_current_time();

        if (ret > 0)
        {
            for (i = 0; i < nb_users; i++)
            {
                if (pollfd[i].revents)
                {
                    fd_poll_event( poll_users[i], pollfd[i].revents );
                    if (!--ret) break;
                }
            }
        }
    }
}


/****************************************************************/
/* device functions */

static struct list_head device_hash[DEVICE_HASH_SIZE];

static int is_device_removable( dev_t dev, int unix_fd )
{
#if defined(linux) && defined(HAVE_FSTATFS)
    struct statfs stfs;

    /* check for floppy disk */
    if (major(dev) == FLOPPY_MAJOR) return 1;

    if (fstatfs( unix_fd, &stfs ) == -1) return 0;
    return (stfs.f_type == 0x9660 ||    /* iso9660 */
            stfs.f_type == 0x9fa1 ||    /* supermount */
            stfs.f_type == 0x15013346); /* udf */
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__DragonFly__) || defined(__APPLE__)
    struct statfs stfs;

    if (fstatfs( unix_fd, &stfs ) == -1) return 0;
    return (!strcmp("cd9660", stfs.f_fstypename) || !strcmp("udf", stfs.f_fstypename));
#elif defined(__NetBSD__)
    struct statvfs stfs;

    if (fstatvfs( unix_fd, &stfs ) == -1) return 0;
    return (!strcmp("cd9660", stfs.f_fstypename) || !strcmp("udf", stfs.f_fstypename));
#elif defined(sun)
# include <sys/dkio.h>
# include <sys/vtoc.h>
    struct dk_cinfo dkinf;
    if (ioctl( unix_fd, DKIOCINFO, &dkinf ) == -1) return 0;
    return (dkinf.dki_ctype == DKC_CDROM ||
            dkinf.dki_ctype == DKC_NCRFLOPPY ||
            dkinf.dki_ctype == DKC_SMSFLOPPY ||
            dkinf.dki_ctype == DKC_INTEL82072 ||
            dkinf.dki_ctype == DKC_INTEL82077);
#else
    return 0;
#endif
}

/* retrieve the device object for a given fd, creating it if needed */
static struct device *get_device( dev_t dev, int unix_fd )
{
    struct device *device;
    unsigned int i, hash = dev % DEVICE_HASH_SIZE;

    if (device_hash[hash].next)
    {
        LIST_FOR_EACH_ENTRY( device, &device_hash[hash], struct device, entry )
            if (device->dev == dev) return (struct device *)grab_object( device );
    }
    else list_init( &device_hash[hash] );

    /* not found, create it */

    if (unix_fd == -1) return NULL;
    if ((device = alloc_object( &device_ops )))
    {
        device->dev = dev;
        device->removable = is_device_removable( dev, unix_fd );
        for (i = 0; i < INODE_HASH_SIZE; i++) list_init( &device->inode_hash[i] );
        wine_list_add_head( &device_hash[hash], &device->entry );
    }
    return device;
}

static void device_dump( struct object *obj, int verbose )
{
    struct device *device = (struct device *)obj;
    fprintf( stderr, "Device dev=" );
    DUMP_LONG_LONG( device->dev );
    fprintf( stderr, "\n" );
}

static void device_destroy( struct object *obj )
{
    struct device *device = (struct device *)obj;
    unsigned int i;

    for (i = 0; i < INODE_HASH_SIZE; i++)
        assert( list_empty(&device->inode_hash[i]) );

    list_remove( &device->entry );  /* remove it from the hash table */
}


/****************************************************************/
/* inode functions */

/* close all pending file descriptors in the closed list */
static void inode_close_pending( struct uk_inode *inode, int keep_unlinks )
{
    struct list_head *ptr = list_head( &inode->closed );

    while (ptr)
    {
        struct closed_fd *fd = LIST_ENTRY( ptr, struct closed_fd, entry );
        struct list_head *next = list_next( &inode->closed, ptr );

        if (fd->unix_fd != -1)
        {
            close( fd->unix_fd );
            fd->unix_fd = -1;
        }
        if (!keep_unlinks || !fd->unlink[0])  /* get rid of it unless there's an unlink pending on that file */
        {
            list_remove( ptr );
            free( fd );
        }
        ptr = next;
    }
}

static void inode_dump( struct object *obj, int verbose )
{
    struct uk_inode *inode = (struct uk_inode *)obj;
    fprintf( stderr, "Inode device=%p ino=", inode->device );
    DUMP_LONG_LONG( inode->ino );
    fprintf( stderr, "\n" );
}

static void inode_destroy( struct object *obj )
{
    struct uk_inode *inode = (struct uk_inode *)obj;
    struct list_head *ptr;

    assert( list_empty(&inode->open) );
    assert( list_empty(&inode->locks) );

    list_remove( &inode->entry );

    while ((ptr = list_head( &inode->closed )))
    {
        struct closed_fd *fd = LIST_ENTRY( ptr, struct closed_fd, entry );
        list_remove( ptr );
        if (fd->unix_fd != -1) close( fd->unix_fd );
        if (fd->unlink[0])
        {
            /* make sure it is still the same file */
            struct stat st;
            if (!stat( fd->unlink, &st ) && st.st_dev == inode->device->dev && st.st_ino == inode->ino)
            {
                if (S_ISDIR(st.st_mode)) rmdir( fd->unlink );
                else unlink( fd->unlink );
            }
        }
        free( fd );
    }
    release_object( inode->device );
}

/* retrieve the inode object for a given fd, creating it if needed */
static struct uk_inode *get_inode( dev_t dev, ino_t ino, int unix_fd )
{
    struct device *device;
    struct uk_inode *inode;
    unsigned int hash = ino % INODE_HASH_SIZE;

    if (!(device = get_device( dev, unix_fd ))) return NULL;

    LIST_FOR_EACH_ENTRY( inode, &device->inode_hash[hash], struct uk_inode, entry )
    {
        if (inode->ino == ino)
        {
            release_object( device );
            return (struct uk_inode *)grab_object( inode );
        }
    }

    /* not found, create it */
    if ((inode = alloc_object( &inode_ops )))
    {
        inode->device = device;
        inode->ino    = ino;
        list_init( &inode->open );
        list_init( &inode->locks );
        list_init( &inode->closed );
        wine_list_add_head( &device->inode_hash[hash], &inode->entry );
    }
    else release_object( device );

    return inode;
}

/* add fd to the inode list of file descriptors to close */
static void inode_add_closed_fd( struct uk_inode *inode, struct closed_fd *fd )
{
    if (!list_empty( &inode->locks ))
    {
        wine_list_add_head( &inode->closed, &fd->entry );
    }
    else if (fd->unlink[0])  /* close the fd but keep the structure around for unlink */
    {
        if (fd->unix_fd != -1) close( fd->unix_fd );
        fd->unix_fd = -1;
        wine_list_add_head( &inode->closed, &fd->entry );
    }
    else  /* no locks on this inode and no unlink, get rid of the fd */
    {
        if (fd->unix_fd != -1) close( fd->unix_fd );
        free( fd );
    }
}


/****************************************************************/
/* file lock functions */

static void file_lock_dump( struct object *obj, int verbose )
{
    struct uk_file_lock *lock = (struct uk_file_lock *)obj;
    fprintf( stderr, "Lock %s fd=%p proc=%p start=",
             lock->shared ? "shared" : "excl", lock->fd, lock->process );
    DUMP_LONG_LONG( lock->start );
    fprintf( stderr, " end=" );
    DUMP_LONG_LONG( lock->end );
    fprintf( stderr, "\n" );
}

static int file_lock_signaled( struct object *obj, struct wait_queue_entry *entry )
{
    struct uk_file_lock *lock = (struct uk_file_lock *)obj;
    /* lock is signaled if it has lost its owner */
    return !lock->process;
}

/* set (or remove) a Unix lock if possible for the given range */
static int set_unix_lock( struct uk_fd *fd, file_pos_t start, file_pos_t end, int type )
{
    struct flock fl;

    if (!fd->fs_locks) return 1;  /* no fs locks possible for this fd */
    for (;;)
    {
        if (start == end) return 1;  /* can't set zero-byte lock */
        if (start > max_unix_offset) return 1;  /* ignore it */
        fl.l_type   = type;
        fl.l_whence = SEEK_SET;
        fl.l_start  = start;
        if (!end || end > max_unix_offset) fl.l_len = 0;
        else fl.l_len = end - start;
        if (fcntl( get_unix_fd(fd), F_SETLK, &fl ) != -1) return 1;

        switch(errno)
        {
        case EACCES:
            /* check whether locks work at all on this file system */
            if (fcntl( get_unix_fd(fd), F_GETLK, &fl ) != -1)
            {
                set_error( STATUS_FILE_LOCK_CONFLICT );
                return 0;
            }
            /* fall through */
        case EIO:
        case ENOLCK:
            /* no locking on this fs, just ignore it */
            fd->fs_locks = 0;
            return 1;
        case EAGAIN:
            set_error( STATUS_FILE_LOCK_CONFLICT );
            return 0;
        case EBADF:
            /* this can happen if we try to set a write lock on a read-only file */
            /* we just ignore that error */
            if (fl.l_type == F_WRLCK) return 1;
            set_error( STATUS_ACCESS_DENIED );
            return 0;
#ifdef EOVERFLOW
        case EOVERFLOW:
#endif
        case EINVAL:
            /* this can happen if off_t is 64-bit but the kernel only supports 32-bit */
            /* in that case we shrink the limit and retry */
            if (max_unix_offset > INT_MAX)
            {
                max_unix_offset = INT_MAX;
                break;  /* retry */
            }
            /* fall through */
        default:
            file_set_error();
            return 0;
        }
    }
}

/* check if interval [start;end) overlaps the lock */
static inline int lock_overlaps( struct uk_file_lock *lock, file_pos_t start, file_pos_t end )
{
    if (lock->end && start >= lock->end) return 0;
    if (end && lock->start >= end) return 0;
    return 1;
}

/* remove Unix locks for all bytes in the specified area that are no longer locked */
static void remove_unix_locks( struct uk_fd *fd, file_pos_t start, file_pos_t end )
{
    struct hole
    {
        struct hole *next;
        struct hole *prev;
        file_pos_t   start;
        file_pos_t   end;
    } *first, *cur, *next, *buffer;

    struct list_head *ptr;
    int count = 0;

    if (!fd->inode) return;
    if (!fd->fs_locks) return;
    if (start == end || start > max_unix_offset) return;
    if (!end || end > max_unix_offset) end = max_unix_offset + 1;

    /* count the number of locks overlapping the specified area */

    LIST_FOR_EACH( ptr, &fd->inode->locks )
    {
        struct uk_file_lock *lock = LIST_ENTRY( ptr, struct uk_file_lock, inode_entry );
        if (lock->start == lock->end) continue;
        if (lock_overlaps( lock, start, end )) count++;
    }

    if (!count)  /* no locks at all, we can unlock everything */
    {
        set_unix_lock( fd, start, end, F_UNLCK );
        return;
    }

    /* allocate space for the list of holes */
    /* max. number of holes is number of locks + 1 */

    if (!(buffer = malloc( sizeof(*buffer) * (count+1) ))) return;
    first = buffer;
    first->next  = NULL;
    first->prev  = NULL;
    first->start = start;
    first->end   = end;
    next = first + 1;

    /* build a sorted list of unlocked holes in the specified area */

    LIST_FOR_EACH( ptr, &fd->inode->locks )
    {
        struct uk_file_lock *lock = LIST_ENTRY( ptr, struct uk_file_lock, inode_entry );
        if (lock->start == lock->end) continue;
        if (!lock_overlaps( lock, start, end )) continue;

        /* go through all the holes touched by this lock */
        for (cur = first; cur; cur = cur->next)
        {
            if (cur->end <= lock->start) continue; /* hole is before start of lock */
            if (lock->end && cur->start >= lock->end) break;  /* hole is after end of lock */

            /* now we know that lock is overlapping hole */

            if (cur->start >= lock->start)  /* lock starts before hole, shrink from start */
            {
                cur->start = lock->end;
                if (cur->start && cur->start < cur->end) break;  /* done with this lock */
                /* now hole is empty, remove it */
                if (cur->next) cur->next->prev = cur->prev;
                if (cur->prev) cur->prev->next = cur->next;
                else if (!(first = cur->next)) goto done;  /* no more holes at all */
            }
            else if (!lock->end || cur->end <= lock->end)  /* lock larger than hole, shrink from end */
            {
                cur->end = lock->start;
                assert( cur->start < cur->end );
            }
            else  /* lock is in the middle of hole, split hole in two */
            {
                next->prev = cur;
                next->next = cur->next;
                cur->next = next;
                next->start = lock->end;
                next->end = cur->end;
                cur->end = lock->start;
                assert( next->start < next->end );
                assert( cur->end < next->start );
                next++;
                break;  /* done with this lock */
            }
        }
    }

    /* clear Unix locks for all the holes */

    for (cur = first; cur; cur = cur->next)
        set_unix_lock( fd, cur->start, cur->end, F_UNLCK );

 done:
    free( buffer );
}

/* create a new lock on a fd */
static struct uk_file_lock *add_lock( struct uk_fd *fd, int shared, file_pos_t start, file_pos_t end )
{
    struct uk_file_lock *lock;

    if (!(lock = alloc_object( &file_lock_ops ))) return NULL;
    lock->shared  = shared;
    lock->start   = start;
    lock->end     = end;
    lock->fd      = fd;
    lock->process = current_thread->process;

    /* now try to set a Unix lock */
    if (!set_unix_lock( lock->fd, lock->start, lock->end, lock->shared ? F_RDLCK : F_WRLCK ))
    {
        release_object( lock );
        return NULL;
    }
    wine_list_add_tail( &fd->locks, &lock->fd_entry );
    wine_list_add_tail( &fd->inode->locks, &lock->inode_entry );
    wine_list_add_tail( &lock->process->locks, &lock->proc_entry );
    return lock;
}

/* remove an existing lock */
static void remove_lock( struct uk_file_lock *lock, int remove_unix )
{
    struct uk_inode *inode = lock->fd->inode;

    list_remove( &lock->fd_entry );
    list_remove( &lock->inode_entry );
    list_remove( &lock->proc_entry );
    if (remove_unix) remove_unix_locks( lock->fd, lock->start, lock->end );
    if (list_empty( &inode->locks )) inode_close_pending( inode, 1 );
    lock->process = NULL;
    uk_wake_up( &lock->obj, 0 );
    release_object( lock );
}

/* remove all locks owned by a given process */
void remove_process_locks( struct process *process )
{
    struct list_head *ptr;

    while ((ptr = list_head( &process->locks )))
    {
        struct uk_file_lock *lock = LIST_ENTRY( ptr, struct uk_file_lock, proc_entry );
        remove_lock( lock, 1 );  /* this removes it from the list */
    }
}

/* remove all locks on a given fd */
static void remove_fd_locks( struct uk_fd *fd )
{
    file_pos_t start = FILE_POS_T_MAX, end = 0;
    struct list_head *ptr;

    while ((ptr = list_head( &fd->locks )))
    {
        struct uk_file_lock *lock = LIST_ENTRY( ptr, struct uk_file_lock, fd_entry );
        if (lock->start < start) start = lock->start;
        if (!lock->end || lock->end > end) end = lock->end - 1;
        remove_lock( lock, 0 );
    }
    if (start < end) remove_unix_locks( fd, start, end + 1 );
}

/* add a lock on an fd */
/* returns handle to wait on */
obj_handle_t lock_fd( struct uk_fd *fd, file_pos_t start, file_pos_t count, int shared, int wait )
{
    struct list_head *ptr;
    file_pos_t end = start + count;

    if (!fd->inode)  /* not a regular file */
    {
        set_error( STATUS_INVALID_DEVICE_REQUEST );
        return 0;
    }

    /* don't allow wrapping locks */
    if (end && end < start)
    {
        set_error( STATUS_INVALID_PARAMETER );
        return 0;
    }

    /* check if another lock on that file overlaps the area */
    LIST_FOR_EACH( ptr, &fd->inode->locks )
    {
        struct uk_file_lock *lock = LIST_ENTRY( ptr, struct uk_file_lock, inode_entry );
        if (!lock_overlaps( lock, start, end )) continue;
        if (shared && (lock->shared || lock->fd == fd)) continue;
        /* found one */
        if (!wait)
        {
            set_error( STATUS_FILE_LOCK_CONFLICT );
            return 0;
        }
        set_error( STATUS_PENDING );
        return alloc_handle( current_thread->process, lock, SYNCHRONIZE, 0 );
    }

    /* not found, add it */
    if (add_lock( fd, shared, start, end )) return 0;
    if (get_error() == STATUS_FILE_LOCK_CONFLICT)
    {
        /* Unix lock conflict -> tell client to wait and retry */
        if (wait) set_error( STATUS_PENDING );
    }
    return 0;
}

/* remove a lock on an fd */
void unlock_fd( struct uk_fd *fd, file_pos_t start, file_pos_t count )
{
    struct list_head *ptr;
    file_pos_t end = start + count;

    /* find an existing lock with the exact same parameters */
    LIST_FOR_EACH( ptr, &fd->locks )
    {
        struct uk_file_lock *lock = LIST_ENTRY( ptr, struct uk_file_lock, fd_entry );
        if ((lock->start == start) && (lock->end == end))
        {
            remove_lock( lock, 1 );
            return;
        }
    }
    set_error( STATUS_FILE_LOCK_CONFLICT );
}


/****************************************************************/
/* file descriptor functions */

static void fd_dump( struct object *obj, int verbose )
{
    struct uk_fd *fd = (struct uk_fd *)obj;
    fprintf( stderr, "Fd unix_fd=%d user=%p options=%08x", fd->unix_fd, fd->user, fd->options );
    if (fd->inode) fprintf( stderr, " inode=%p unlink='%s'", fd->inode, fd->closed->unlink );
    fprintf( stderr, "\n" );
}

static void fd_destroy( struct object *obj )
{
    struct uk_fd *fd = (struct uk_fd *)obj;

    free_async_queue( fd->read_q );
    free_async_queue( fd->write_q );
    free_async_queue( fd->wait_q );

    if (fd->completion) release_object( fd->completion );
    remove_fd_locks( fd );
    free( fd->unix_name );
    list_remove( &fd->inode_entry );
    if (fd->poll_index != -1) remove_poll_user( fd, fd->poll_index );
#ifdef CONFIG_UNIFIED_KERNEL
    uk_poll_freewait(&fd->uk_pwq);
    destroy_map_tbl( fd ); /* fd->unix_fd will be closed by destroy_map_tbl */
    if (fd->inode)
    {
        inode_add_closed_fd( fd->inode, fd->closed );
        release_object( fd->inode );
    }
#else
    else  /* no inode, close it right away */
    {
        if (fd->unix_fd != -1) close( fd->unix_fd );
    }
#endif
}

/* check if the desired access is possible without violating */
/* the sharing mode of other opens of the same file */
static unsigned int check_sharing( struct uk_fd *fd, unsigned int access, unsigned int sharing,
                                   unsigned int open_flags, unsigned int options )
{
    /* only a few access bits are meaningful wrt sharing */
    const unsigned int read_access = FILE_READ_DATA | FILE_EXECUTE;
    const unsigned int write_access = FILE_WRITE_DATA | FILE_APPEND_DATA;
    const unsigned int all_access = read_access | write_access | DELETE;

    unsigned int existing_sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    unsigned int existing_access = 0;
    struct list_head *ptr;

    fd->access = access;
    fd->sharing = sharing;

    LIST_FOR_EACH( ptr, &fd->inode->open )
    {
        struct uk_fd *fd_ptr = LIST_ENTRY( ptr, struct uk_fd, inode_entry );
        if (fd_ptr != fd)
        {
            /* if access mode is 0, sharing mode is ignored */
            if (fd_ptr->access & all_access) existing_sharing &= fd_ptr->sharing;
            existing_access |= fd_ptr->access;
        }
    }

    if (((access & read_access) && !(existing_sharing & FILE_SHARE_READ)) ||
        ((access & write_access) && !(existing_sharing & FILE_SHARE_WRITE)) ||
        ((access & DELETE) && !(existing_sharing & FILE_SHARE_DELETE)))
        return STATUS_SHARING_VIOLATION;
    if (((existing_access & FILE_MAPPING_WRITE) && !(sharing & FILE_SHARE_WRITE)) ||
        ((existing_access & FILE_MAPPING_IMAGE) && (access & FILE_WRITE_DATA)))
        return STATUS_SHARING_VIOLATION;
    if ((existing_access & FILE_MAPPING_IMAGE) && (options & FILE_DELETE_ON_CLOSE))
        return STATUS_CANNOT_DELETE;
    if ((existing_access & FILE_MAPPING_ACCESS) && (open_flags & O_TRUNC))
        return STATUS_USER_MAPPED_FILE;
    if (!(access & all_access))
        return 0;  /* if access mode is 0, sharing mode is ignored (except for mappings) */
    if (((existing_access & read_access) && !(sharing & FILE_SHARE_READ)) ||
        ((existing_access & write_access) && !(sharing & FILE_SHARE_WRITE)) ||
        ((existing_access & DELETE) && !(sharing & FILE_SHARE_DELETE)))
        return STATUS_SHARING_VIOLATION;
    return 0;
}

/* set the events that select waits for on this fd */
#ifdef CONFIG_UNIFIED_KERNEL
void set_fd_events( struct uk_fd *fd, int events )
{
    struct file *filp = get_unix_file( fd );

    if (!filp) goto done;

    if (events == -1)  /* stop waiting on this fd completely */
    {
        if (atomic_read(&fd->state)==FD_REMOVED) goto done;  /* already removed */
        uk_remove_fd_events(&fd->uk_pwq);
        atomic_set(&fd->state,FD_REMOVED);
    }
    else if (atomic_read(&fd->state) != FD_ADDED)
    {
        if (fd->events) goto done;  /* stopped waiting on it, don't restart */
        uk_add_fd_events(fd, filp, events);
        return;
    }
    else
    {
        if (fd->events == events) goto done;  /* nothing to do */
        uk_modify_fd_events(fd, filp, events);
        return;
    }

done:
    if (events == -1)  /* stop waiting on this fd completely */
    {
        fd->events = POLLERR;
    }
    else if (atomic_read(&fd->state)==FD_ADDED || !fd->events)
    {
        fd->events = events;
    }
}
#else
void set_fd_events( struct uk_fd *fd, int events )
{
    int user = fd->poll_index;
    assert( poll_users[user] == fd );

    set_fd_epoll_events( fd, user, events );

    if (events == -1)  /* stop waiting on this fd completely */
    {
        pollfd[user].fd = -1;
        pollfd[user].events = POLLERR;
        pollfd[user].revents = 0;
    }
    else if (pollfd[user].fd != -1 || !pollfd[user].events)
    {
        pollfd[user].fd = get_unix_fd(fd);
        pollfd[user].events = events;
    }
}
#endif

/* prepare an fd for unmounting its corresponding device */
static inline void unmount_fd( struct uk_fd *fd )
{
    assert( fd->inode );

    async_wake_up( fd->read_q, STATUS_VOLUME_DISMOUNTED );
    async_wake_up( fd->write_q, STATUS_VOLUME_DISMOUNTED );

    if (fd->poll_index != -1) set_fd_events( fd, -1 );

    if (get_unix_fd(fd) != -1) close( get_unix_fd(fd) );

    fd->unix_fd = -1;
    fd->no_fd_status = STATUS_VOLUME_DISMOUNTED;
    fd->closed->unix_fd = -1;
    fd->closed->unlink[0] = 0;

    /* stop using Unix locks on this fd (existing locks have been removed by close) */
    fd->fs_locks = 0;
}

/* allocate an fd object, without setting the unix fd yet */
static struct uk_fd *alloc_fd_object(void)
{
    struct uk_fd *fd = alloc_object( &fd_ops );

    if (!fd) return NULL;

    fd->fd_ops     = NULL;
    fd->user       = NULL;
    fd->inode      = NULL;
    fd->closed     = NULL;
    fd->access     = 0;
    fd->options    = 0;
    fd->sharing    = 0;
    fd->unix_fd    = -1;
    fd->unix_name  = NULL;
    fd->cacheable  = 0;
    fd->signaled   = 1;
    fd->fs_locks   = 1;
    fd->poll_index = -1;
    fd->read_q     = NULL;
    fd->write_q    = NULL;
    fd->wait_q     = NULL;
    fd->completion = NULL;
    list_init( &fd->inode_entry );
    list_init( &fd->locks );
#ifdef CONFIG_UNIFIED_KERNEL
    fd->events = 0;
    atomic_set(&fd->state, FD_UNINIT);
    fd->uk_pwq.have_inited_flag = false;
    fd->creator_pid = 0;
    fd->unix_file = NULL;
    fd->tbl_index = 0;
    fd->map_tbl = malloc(sizeof(struct pid_fd_map) * DEFAULT_MAP_NUM);
    if (!fd->map_tbl)
    {
        klog(0," malloc error \n");
        fd->max_index = 0;
    }
    else
    {
        memset(fd->map_tbl, -1, sizeof(struct pid_fd_map) * DEFAULT_MAP_NUM);
        fd->max_index = DEFAULT_MAP_NUM;
    }
#else

    if ((fd->poll_index = add_poll_user( fd )) == -1)
    {
        release_object( fd );
        return NULL;
    }
#endif
    return fd;
}

/* allocate a pseudo fd object, for objects that need to behave like files but don't have a unix fd */
struct uk_fd *alloc_pseudo_fd( const struct fd_ops *fd_user_ops, struct object *user, unsigned int options )
{
    struct uk_fd *fd = alloc_object( &fd_ops );

    if (!fd) return NULL;

    fd->fd_ops     = fd_user_ops;
    fd->user       = user;
    fd->inode      = NULL;
    fd->closed     = NULL;
    fd->access     = 0;
    fd->options    = options;
    fd->sharing    = 0;
    fd->unix_name  = NULL;
    fd->unix_fd    = -1;
    fd->cacheable  = 0;
    fd->signaled   = 0;
    fd->fs_locks   = 0;
    fd->poll_index = -1;
    fd->read_q     = NULL;
    fd->write_q    = NULL;
    fd->wait_q     = NULL;
    fd->completion = NULL;
    fd->no_fd_status = STATUS_BAD_DEVICE_TYPE;
    list_init( &fd->inode_entry );
    list_init( &fd->locks );
#ifdef CONFIG_UNIFIED_KERNEL
    fd->events = 0;
    atomic_set(&fd->state, FD_UNINIT);
    fd->uk_pwq.have_inited_flag = false;
    fd->creator_pid = 0;
    fd->unix_file = NULL;
    fd->tbl_index = 0;
    fd->map_tbl = malloc(sizeof(struct pid_fd_map) * DEFAULT_MAP_NUM);
    if (!fd->map_tbl)
    {
        klog(0," malloc error \n");
        fd->max_index = 0;
    }
    else
    {
        memset(fd->map_tbl, -1, sizeof(struct pid_fd_map) * DEFAULT_MAP_NUM);
        fd->max_index = DEFAULT_MAP_NUM;
    }
#endif
    return fd;
}

/* duplicate an fd object for a different user */
struct uk_fd *dup_fd_object( struct uk_fd *orig, unsigned int access, unsigned int sharing, unsigned int options )
{
    unsigned int err;
    struct uk_fd *fd = alloc_fd_object();

    if (!fd) return NULL;

    fd->options    = options;
    fd->cacheable  = orig->cacheable;

    if (orig->unix_name)
    {
        if (!(fd->unix_name = mem_alloc( strlen(orig->unix_name) + 1 ))) goto failed;
        strcpy( fd->unix_name, orig->unix_name );
    }

#ifdef CONFIG_UNIFIED_KERNEL
    if (orig->inode)
    {
        struct closed_fd *closed = mem_alloc( sizeof(*closed) );
        if (!closed) goto failed;
        if (orig->creator_pid == current->pid)
        {
            if ((fd->unix_fd = dup( orig->unix_fd )) == -1)
            {
                file_set_error();
                free( closed );
                goto failed;
            }
        }
        else
        {
            int new_fd = -1;
            new_fd = get_unused_fd();
            if (new_fd<0)
            {
                klog(0,"get_unused_fd() error %d\n",new_fd);
                errno = -new_fd;
                file_set_error();
                goto failed;
            }

            fd_install(new_fd, orig->unix_file);
            get_file(orig->unix_file);
            fd->unix_fd = new_fd;
        }
        fd->creator_pid = current->pid;
        fd->unix_file = orig->unix_file;
        fd->map_tbl[fd->tbl_index].pid = current->pid;
        fd->map_tbl[fd->tbl_index].unix_fd = fd->unix_fd;
        fd->tbl_index++;

        closed->unix_fd = -1;
        closed->unlink[0] = 0;
        fd->closed = closed;
        fd->inode = (struct uk_inode *)grab_object( orig->inode );
        wine_list_add_head( &fd->inode->open, &fd->inode_entry );
        if ((err = check_sharing( fd, access, sharing, 0, options )))
        {
            set_error( err );
            goto failed;
        }
    }
    else
    {
        if (orig->creator_pid == current->pid)
        {
            if ((fd->unix_fd = dup( orig->unix_fd )) == -1)
            {
                file_set_error();
                goto failed;
            }
        }
        else
        {
            int new_fd = -1;
            new_fd = get_unused_fd();
            if (new_fd<0)
            {
                klog(0,"get_unused_fd() error %d\n",new_fd);
                errno = -new_fd;
                file_set_error();
                goto failed;
            }

            fd_install(new_fd, orig->unix_file);
            get_file(orig->unix_file);
            fd->unix_fd = new_fd;
        }
        fd->creator_pid = current->pid;
        fd->unix_file = orig->unix_file;
        fd->map_tbl[fd->tbl_index].pid = current->pid;
        fd->map_tbl[fd->tbl_index].unix_fd = fd->unix_fd;
        fd->tbl_index++;
    }
#else
    if (orig->inode)
    {
        struct closed_fd *closed = mem_alloc( sizeof(*closed) );
        if (!closed) goto failed;
        if ((fd->unix_fd = dup( orig->unix_fd )) == -1)
        {
            file_set_error();
            free( closed );
            goto failed;
        }
        closed->unix_fd = fd->unix_fd;
        closed->unlink[0] = 0;
        fd->closed = closed;
        fd->inode = (struct uk_inode *)grab_object( orig->inode );
        wine_list_add_head( &fd->inode->open, &fd->inode_entry );
        if ((err = check_sharing( fd, access, sharing, 0, options )))
        {
            set_error( err );
            goto failed;
        }
    }
    else if ((fd->unix_fd = dup( orig->unix_fd )) == -1)
    {
        file_set_error();
        goto failed;
    }
#endif
    return fd;

failed:
    release_object( fd );
    return NULL;
}

/* find an existing fd object that can be reused for a mapping */
struct uk_fd *get_fd_object_for_mapping( struct uk_fd *fd, unsigned int access, unsigned int sharing )
{
    struct uk_fd *fd_ptr;

    if (!fd->inode) return NULL;

    LIST_FOR_EACH_ENTRY( fd_ptr, &fd->inode->open, struct uk_fd, inode_entry )
        if (fd_ptr->access == access && fd_ptr->sharing == sharing)
            return (struct uk_fd *)grab_object( fd_ptr );

    return NULL;
}

/* set the status to return when the fd has no associated unix fd */
void set_no_fd_status( struct uk_fd *fd, unsigned int status )
{
    fd->no_fd_status = status;
}

/* sets the user of an fd that previously had no user */
void set_fd_user( struct uk_fd *fd, const struct fd_ops *user_ops, struct object *user )
{
    assert( fd->fd_ops == NULL );
    fd->fd_ops = user_ops;
    fd->user   = user;
}

static char *dup_fd_name( struct uk_fd *root, const char *name )
{
    char *ret;

    if (!root) return strdup( name );
    if (!root->unix_name) return NULL;

    /* skip . prefix */
    if (name[0] == '.' && (!name[1] || name[1] == '/')) name++;

    if ((ret = malloc( strlen(root->unix_name) + strlen(name) + 2 )))
    {
        strcpy( ret, root->unix_name );
        if (name[0] && name[0] != '/') strcat( ret, "/" );
        strcat( ret, name );
    }
    return ret;
}

/* open() wrapper that returns a struct uk_fd with no fd user set */
struct uk_fd *open_fd( struct uk_fd *root, const char *name, int flags, mode_t *mode, unsigned int access,
                    unsigned int sharing, unsigned int options )
{
    struct stat st;
    struct closed_fd *closed_fd;
    struct uk_fd *fd;
    const char *unlink_name = "";
    int root_fd = -1;
    int rw_mode;

    if (((options & FILE_DELETE_ON_CLOSE) && !(access & DELETE)) ||
        ((options & FILE_DIRECTORY_FILE) && (flags & O_TRUNC)))
    {
        set_error( STATUS_INVALID_PARAMETER );
        return NULL;
    }

    if (!(fd = alloc_fd_object())) return NULL;

    fd->options = options;
    if (options & FILE_DELETE_ON_CLOSE) unlink_name = name;
    if (!(closed_fd = mem_alloc( sizeof(*closed_fd) + strlen(unlink_name) )))
    {
        release_object( fd );
        return NULL;
    }

    if (root)
    {
        if ((root_fd = get_unix_fd( root )) == -1) goto error;
        if (fchdir( root_fd ) == -1)
        {
            file_set_error();
            root_fd = -1;
            goto error;
        }
    }

    /* create the directory if needed */
    if ((options & FILE_DIRECTORY_FILE) && (flags & O_CREAT))
    {
        if (mkdir( name, *mode ) == -1)
        {
            if (errno != EEXIST || (flags & O_EXCL))
            {
                file_set_error();
                goto error;
            }
        }
        flags &= ~(O_CREAT | O_EXCL | O_TRUNC);
    }

    if ((access & FILE_UNIX_WRITE_ACCESS) && !(options & FILE_DIRECTORY_FILE))
    {
        if (access & FILE_UNIX_READ_ACCESS) rw_mode = O_RDWR;
        else rw_mode = O_WRONLY;
    }
    else rw_mode = O_RDONLY;

    fd->unix_name = dup_fd_name( root, name );

    if ((fd->unix_fd = open( name, rw_mode | (flags & ~O_TRUNC), *mode )) == -1)
    {
        /* if we tried to open a directory for write access, retry read-only */
        if (errno == EISDIR)
        {
            if ((access & FILE_UNIX_WRITE_ACCESS) || (flags & O_CREAT))
                fd->unix_fd = open( name, O_RDONLY | (flags & ~(O_TRUNC | O_CREAT | O_EXCL)), *mode );
        }

        if (fd->unix_fd == -1)
        {
            file_set_error();
            goto error;
        }
    }

#ifdef CONFIG_UNIFIED_KERNEL
    fd->creator_pid = current->pid;
    fd->unix_file = fget(fd->unix_fd);
    if (!fd->unix_file)
    {
        klog(0,"fget error \n");
    }
    else
    {
        fd->map_tbl[fd->tbl_index].pid = current->pid;
        fd->map_tbl[fd->tbl_index].unix_fd = fd->unix_fd;
        fd->tbl_index++;
        fput(fd->unix_file);
    }

    closed_fd->unix_fd = -1; /* don't use closed_fd->unix_fd */
#else
    closed_fd->unix_fd = fd->unix_fd;
#endif
    closed_fd->unlink[0] = 0;
    fstat( fd->unix_fd, &st );
    *mode = st.st_mode;

    /* only bother with an inode for normal files and directories */
    if (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))
    {
        unsigned int err;
        struct uk_inode *inode = get_inode( st.st_dev, st.st_ino, fd->unix_fd );

        if (!inode)
        {
            /* we can close the fd because there are no others open on the same file,
             * otherwise we wouldn't have failed to allocate a new inode
             */
            goto error;
        }
        fd->inode = inode;
        fd->closed = closed_fd;
        fd->cacheable = !inode->device->removable;
        wine_list_add_head( &inode->open, &fd->inode_entry );

        /* check directory options */
        if ((options & FILE_DIRECTORY_FILE) && !S_ISDIR(st.st_mode))
        {
            release_object( fd );
            set_error( STATUS_NOT_A_DIRECTORY );
            return NULL;
        }
        if ((options & FILE_NON_DIRECTORY_FILE) && S_ISDIR(st.st_mode))
        {
            release_object( fd );
            set_error( STATUS_FILE_IS_A_DIRECTORY );
            return NULL;
        }
        if ((err = check_sharing( fd, access, sharing, flags, options )))
        {
            release_object( fd );
            set_error( err );
            return NULL;
        }
        strcpy( closed_fd->unlink, unlink_name );
        if (flags & O_TRUNC)
        {
            if (S_ISDIR(st.st_mode))
            {
                release_object( fd );
                set_error( STATUS_OBJECT_NAME_COLLISION );
                return NULL;
            }
            ftruncate( fd->unix_fd, 0 );
        }
    }
    else  /* special file */
    {
        if (options & FILE_DIRECTORY_FILE)
        {
            set_error( STATUS_NOT_A_DIRECTORY );
            goto error;
        }
        if (unlink_name[0])  /* we can't unlink special files */
        {
            set_error( STATUS_INVALID_PARAMETER );
            goto error;
        }
        free( closed_fd );
        fd->cacheable = 1;
    }
    return fd;

error:
    release_object( fd );
    free( closed_fd );
    if (root_fd != -1) fchdir( server_dir_fd ); /* go back to the server dir */
    return NULL;
}

/* create an fd for an anonymous file */
/* if the function fails the unix fd is closed */
struct uk_fd *create_anonymous_fd( const struct fd_ops *fd_user_ops, int unix_fd, struct object *user,
                                unsigned int options )
{
    struct uk_fd *fd = alloc_fd_object();

    if (fd)
    {
        set_fd_user( fd, fd_user_ops, user );
        fd->unix_fd = unix_fd;
        fd->options = options;
#ifdef CONFIG_UNIFIED_KERNEL
        fd->creator_pid = current->pid;
        fd->unix_file = fget(unix_fd);
        if (!fd->unix_file)
        {
            klog(0,"fget error unix_fd=%d\n",unix_fd);
        }
        else
        {
            fd->map_tbl[fd->tbl_index].pid = current->pid;
            fd->map_tbl[fd->tbl_index].unix_fd = unix_fd;
            fd->tbl_index++;
            fput(fd->unix_file);
        }
#endif
        return fd;
    }
    close( unix_fd );
    return NULL;
}

/* retrieve the object that is using an fd */
void *get_fd_user( struct uk_fd *fd )
{
    return fd->user;
}

/* retrieve the opening options for the fd */
unsigned int get_fd_options( struct uk_fd *fd )
{
    return fd->options;
}

#ifdef CONFIG_UNIFIED_KERNEL

int find_unix_fd_by_pid(struct uk_fd* fd, pid_t pid)
{
    int i=0;

    if (!fd->map_tbl || fd->tbl_index==0)
        return -1;

    for(i=0; i<fd->tbl_index; i++)
    {
        if (fd->map_tbl[i].pid == pid)
            return fd->map_tbl[i].unix_fd;
    }

    return -1;
}

int get_unix_fd_by_pid(struct uk_fd *fd, pid_t pid)
{
    int new_fd;

    if (!fd->unix_file)
    {
        klog(0,"fd->unix_file is NULL\n");
        return -1;
    }

    new_fd = find_unix_fd_by_pid(fd, pid);
    if (new_fd != -1)
    {
        return new_fd;
    }

    /* not found , alloc one */
    new_fd = get_unused_fd();
    if (new_fd<0)
    {
        klog(0,"get_unused_fd() error \n");
        return -1;
    }

    fd_install(new_fd, fd->unix_file);
    get_file(fd->unix_file); /* reference count inc, close will dec */

    if (fd->tbl_index < fd->max_index)
    {
        fd->map_tbl[fd->tbl_index].pid = pid;
        fd->map_tbl[fd->tbl_index].unix_fd = new_fd;
        fd->tbl_index++;
        return new_fd;
    }
    else /*expend table*/
    {
        struct pid_fd_map *new_tbl;
        int new_size = fd->max_index + fd->max_index/2;

        klog(0,"need expend table %d -> %d\n",fd->max_index, new_size);

        if (in_softirq())
        {
            new_tbl = realloc_atomic(fd->map_tbl, sizeof(struct pid_fd_map) * new_size);
        }
        else
        {
            new_tbl = realloc(fd->map_tbl, sizeof(struct pid_fd_map) * new_size);
        }

        if (!new_tbl)
        {
            klog(0, "realloc error \n");
            return -1;
        }
        else
        {
            fd->map_tbl = new_tbl;
            fd->map_tbl[fd->tbl_index].pid = pid;
            fd->map_tbl[fd->tbl_index].unix_fd = new_fd;
            fd->tbl_index++;
            fd->max_index = new_size;
            return new_fd;
        }
    }
}

void destroy_map_tbl(struct uk_fd *fd)
{
    if (fd->map_tbl)
    {
        if (fd->tbl_index != 0)
        {
            int i;
            for(i=0; i<fd->tbl_index; i++)
            {
                if (fd->map_tbl[i].pid == current->pid)
                    close(fd->map_tbl[i].unix_fd);
                else
                    close_fd_by_pid(fd->map_tbl[i].unix_fd, fd->map_tbl[i].pid);
            }
        }

        free(fd->map_tbl);
        fd->map_tbl = NULL;
    }
}

/* retrieve the unix fd for an object */
int get_unix_fd( struct uk_fd *fd )
{
    if (unlikely(fd->unix_fd == -1))
    {
        set_error( fd->no_fd_status );
        return -1;
    }
    else if (likely(fd->creator_pid == current->pid))
    {
        return fd->unix_fd;
    }
    else
    {
        return get_unix_fd_by_pid(fd, current->pid);
    }
}

struct file *get_unix_file( struct uk_fd *fd )
{
    if (fd->unix_file)
    {
        return fd->unix_file;
    }
    else
    {
        klog(0,"error:fd->unix_file is NULL \n");
        return NULL;
    }
}
#else
/* retrieve the unix fd for an object */
int get_unix_fd( struct uk_fd *fd )
{
    if (fd->unix_fd == -1) set_error( fd->no_fd_status );
    return fd->unix_fd;
}
#endif

/* check if two file descriptors point to the same file */
int is_same_file_fd( struct uk_fd *fd1, struct uk_fd *fd2 )
{
    return fd1->inode == fd2->inode;
}

/* allow the fd to be cached (can't be reset once set) */
void allow_fd_caching( struct uk_fd *fd )
{
    fd->cacheable = 1;
}

/* check if fd is on a removable device */
int is_fd_removable( struct uk_fd *fd )
{
    return (fd->inode && fd->inode->device->removable);
}

/* set or clear the fd signaled state */
void set_fd_signaled( struct uk_fd *fd, int signaled )
{
    fd->signaled = signaled;
    if (signaled) uk_wake_up( fd->user, 0 );
}

/* set or clear the fd signaled state */
int is_fd_signaled( struct uk_fd *fd )
{
    return fd->signaled;
}

/* handler for close_handle that refuses to close fd-associated handles in other processes */
int fd_close_handle( struct object *obj, struct process *process, obj_handle_t handle )
{
    return (!current_thread || current_thread->process == process);
}

/* check if events are pending and if yes return which one(s) */
#ifdef CONFIG_UNIFIED_KERNEL
int check_fd_events( struct uk_fd *fd, int events )
{
    struct file *filp;
    int mask = 0;

    if (fd->inode) return events;  /* regular files are always signaled */

    filp = get_unix_file(fd);
    if (!filp) return POLLERR;
    if (filp->f_op->poll)
    {
        mask = filp->f_op->poll(filp, NULL);
    }
    return mask & events;
}
#else
int check_fd_events( struct uk_fd *fd, int events )
{
    struct pollfd pfd;

    if (get_unix_fd(fd) == -1) return POLLERR;
    if (fd->inode) return events;  /* regular files are always signaled */

    pfd.fd     = get_unix_fd(fd);
    pfd.events = events;
    if (poll( &pfd, 1, 0 ) <= 0) return 0;
    return pfd.revents;
}
#endif

/* default signaled() routine for objects that poll() on an fd */
int default_fd_signaled( struct object *obj, struct wait_queue_entry *entry )
{
    struct uk_fd *fd = get_obj_fd( obj );
    int ret = fd->signaled;
    release_object( fd );
    return ret;
}

/* default map_access() routine for objects that behave like an fd */
unsigned int default_fd_map_access( struct object *obj, unsigned int access )
{
    if (access & GENERIC_READ)    access |= FILE_GENERIC_READ;
    if (access & GENERIC_WRITE)   access |= FILE_GENERIC_WRITE;
    if (access & GENERIC_EXECUTE) access |= FILE_GENERIC_EXECUTE;
    if (access & GENERIC_ALL)     access |= FILE_ALL_ACCESS;
    return access & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

int default_fd_get_poll_events( struct uk_fd *fd )
{
    int events = 0;

    if (async_waiting( fd->read_q )) events |= POLLIN;
    if (async_waiting( fd->write_q )) events |= POLLOUT;
    return events;
}

/* default handler for poll() events */
void default_poll_event( struct uk_fd *fd, int event )
{
    if (event & (POLLIN | POLLERR | POLLHUP)) async_wake_up( fd->read_q, STATUS_ALERTED );
    if (event & (POLLOUT | POLLERR | POLLHUP)) async_wake_up( fd->write_q, STATUS_ALERTED );

    /* if an error occurred, stop polling this fd to avoid busy-looping */
    if (event & (POLLERR | POLLHUP)) set_fd_events( fd, -1 );
    else if (!fd->inode) set_fd_events( fd, fd->fd_ops->get_poll_events( fd ) );
}

struct async *fd_queue_async( struct uk_fd *fd, const async_data_t *data, int type )
{
    struct async_queue *queue;
    struct async *async;

    switch (type)
    {
    case ASYNC_TYPE_READ:
        if (!fd->read_q && !(fd->read_q = create_async_queue( fd ))) return NULL;
        queue = fd->read_q;
        break;
    case ASYNC_TYPE_WRITE:
        if (!fd->write_q && !(fd->write_q = create_async_queue( fd ))) return NULL;
        queue = fd->write_q;
        break;
    case ASYNC_TYPE_WAIT:
        if (!fd->wait_q && !(fd->wait_q = create_async_queue( fd ))) return NULL;
        queue = fd->wait_q;
        break;
    default:
        queue = NULL;
        assert(0);
    }

    if ((async = create_async( current_thread, queue, data )) && type != ASYNC_TYPE_WAIT)
    {
        if (!fd->inode)
            set_fd_events( fd, fd->fd_ops->get_poll_events( fd ) );
        else  /* regular files are always ready for read and write */
            async_wake_up( queue, STATUS_ALERTED );
    }
    return async;
}

void fd_async_wake_up( struct uk_fd *fd, int type, unsigned int status )
{
    switch (type)
    {
    case ASYNC_TYPE_READ:
        async_wake_up( fd->read_q, status );
        break;
    case ASYNC_TYPE_WRITE:
        async_wake_up( fd->write_q, status );
        break;
    case ASYNC_TYPE_WAIT:
        async_wake_up( fd->wait_q, status );
        break;
    default:
        assert(0);
    }
}

void fd_reselect_async( struct uk_fd *fd, struct async_queue *queue )
{
    fd->fd_ops->reselect_async( fd, queue );
}

void no_fd_queue_async( struct uk_fd *fd, const async_data_t *data, int type, int count )
{
    set_error( STATUS_OBJECT_TYPE_MISMATCH );
}

void default_fd_queue_async( struct uk_fd *fd, const async_data_t *data, int type, int count )
{
    struct async *async;

    if ((async = fd_queue_async( fd, data, type )))
    {
        release_object( async );
        set_error( STATUS_PENDING );
    }
}

/* default reselect_async() fd routine */
void default_fd_reselect_async( struct uk_fd *fd, struct async_queue *queue )
{
    if (queue != fd->wait_q)
    {
        int poll_events = fd->fd_ops->get_poll_events( fd );
        int events = check_fd_events( fd, poll_events );
        if (events) fd->fd_ops->poll_event( fd, events );
        else set_fd_events( fd, poll_events );
    }
}

/* default cancel_async() fd routine */
void default_fd_cancel_async( struct uk_fd *fd, struct process *process, struct thread *thread, client_ptr_t iosb )
{
    int n = 0;

    n += async_wake_up_by( fd->read_q, process, thread, iosb, STATUS_CANCELLED );
    n += async_wake_up_by( fd->write_q, process, thread, iosb, STATUS_CANCELLED );
    n += async_wake_up_by( fd->wait_q, process, thread, iosb, STATUS_CANCELLED );
    if (!n && iosb)
        set_error( STATUS_NOT_FOUND );
}

/* default flush() routine */
void no_flush( struct uk_fd *fd, struct event **event )
{
    set_error( STATUS_OBJECT_TYPE_MISMATCH );
}

static inline int is_valid_mounted_device( struct stat *st )
{
#if defined(linux) || defined(__sun__)
    return S_ISBLK( st->st_mode );
#else
    /* disks are char devices on *BSD */
    return S_ISCHR( st->st_mode );
#endif
}

/* close all Unix file descriptors on a device to allow unmounting it */
static void unmount_device( struct uk_fd *device_fd )
{
    unsigned int i;
    struct stat st;
    struct device *device;
    struct uk_inode *inode;
    struct uk_fd *fd;
    int unix_fd = get_unix_fd( device_fd );

    if (unix_fd == -1) return;

    if (fstat( unix_fd, &st ) == -1 || !is_valid_mounted_device( &st ))
    {
        set_error( STATUS_INVALID_PARAMETER );
        return;
    }

    if (!(device = get_device( st.st_rdev, -1 ))) return;

    for (i = 0; i < INODE_HASH_SIZE; i++)
    {
        LIST_FOR_EACH_ENTRY( inode, &device->inode_hash[i], struct uk_inode, entry )
        {
            LIST_FOR_EACH_ENTRY( fd, &inode->open, struct uk_fd, inode_entry )
            {
                unmount_fd( fd );
            }
            inode_close_pending( inode, 0 );
        }
    }
    /* remove it from the hash table */
    list_remove( &device->entry );
    list_init( &device->entry );
    release_object( device );
}

obj_handle_t no_fd_ioctl( struct uk_fd *fd, ioctl_code_t code, const async_data_t *async,
                          int blocking, const void *data, data_size_t size )
{
    set_error( STATUS_OBJECT_TYPE_MISMATCH );
    return 0;
}

/* default ioctl() routine */
obj_handle_t default_fd_ioctl( struct uk_fd *fd, ioctl_code_t code, const async_data_t *async,
                               int blocking, const void *data, data_size_t size )
{
    switch(code)
    {
    case FSCTL_DISMOUNT_VOLUME:
        unmount_device( fd );
        return 0;
    default:
        set_error( STATUS_NOT_SUPPORTED );
        return 0;
    }
}

/* same as get_handle_obj but retrieve the struct uk_fd associated to the object */
static struct uk_fd *get_handle_fd_obj( struct process *process, obj_handle_t handle,
                                     unsigned int access )
{
    struct uk_fd *fd = NULL;
    struct object *obj;

    if ((obj = get_handle_obj( process, handle, access, NULL )))
    {
        fd = get_obj_fd( obj );
        release_object( obj );
    }
    return fd;
}

struct uk_completion *fd_get_completion( struct uk_fd *fd, apc_param_t *p_key )
{
    *p_key = fd->comp_key;
    return fd->completion ? (struct uk_completion *)grab_object( fd->completion ) : NULL;
}

void fd_copy_completion( struct uk_fd *src, struct uk_fd *dst )
{
    assert( !dst->completion );
    dst->completion = fd_get_completion( src, &dst->comp_key );
}

/* flush a file buffers */
DECL_HANDLER(flush_file)
{
    struct uk_fd *fd = get_handle_fd_obj( current_thread->process, req->handle, 0 );
    struct event * event = NULL;

    if (fd)
    {
        fd->fd_ops->flush( fd, &event );
        if ( event )
        {
            reply->event = alloc_handle( current_thread->process, event, SYNCHRONIZE, 0 );
        }
        release_object( fd );
    }
}

/* open a file object */
DECL_HANDLER(open_file_object)
{
    struct unicode_str name;
    struct directory *root = NULL;
    struct object *obj, *result;

    get_req_unicode_str( &name );
    if (req->rootdir && !(root = get_directory_obj( current_thread->process, req->rootdir, 0 )))
        return;

    if ((obj = open_object_dir( root, &name, req->attributes, NULL )))
    {
        if ((result = obj->ops->open_file( obj, req->access, req->sharing, req->options )))
        {
            reply->handle = alloc_handle( current_thread->process, result, req->access, req->attributes );
            release_object( result );
        }
        release_object( obj );
    }

    if (root) release_object( root );
}

/* get the Unix name from a file handle */
DECL_HANDLER(get_handle_unix_name)
{
    struct uk_fd *fd;

    if ((fd = get_handle_fd_obj( current_thread->process, req->handle, 0 )))
    {
        if (fd->unix_name)
        {
            data_size_t name_len = strlen( fd->unix_name );
            reply->name_len = name_len;
            if (name_len <= get_reply_max_size()) set_reply_data( fd->unix_name, name_len );
            else set_error( STATUS_BUFFER_OVERFLOW );
        }
        else set_error( STATUS_OBJECT_TYPE_MISMATCH );
        release_object( fd );
    }
}

/* get a Unix fd to access a file */
DECL_HANDLER(get_handle_fd)
{
    struct uk_fd *fd;

    if ((fd = get_handle_fd_obj( current_thread->process, req->handle, 0 )))
    {
        int unix_fd = get_unix_fd( fd );
        if (unix_fd != -1)
        {
            reply->type = fd->fd_ops->get_fd_type( fd );
            reply->cacheable = fd->cacheable;
            reply->options = fd->options;
            reply->access = get_handle_access( current_thread->process, req->handle );
#ifdef CONFIG_UNIFIED_KERNEL
            unix_fd = dup(unix_fd); /* the new_fd will be closed by UserApplication */
            if (unix_fd >= 0)
                reply->fd = unix_fd;
            else
                klog(0,"dup error unix_fd=%d\n",unix_fd);
#else
            send_client_fd( current_thread->process, unix_fd, req->handle );
#endif
        }
        release_object( fd );
    }
}

/* perform an ioctl on a file */
DECL_HANDLER(ioctl)
{
    unsigned int access = (req->code >> 14) & (FILE_READ_DATA|FILE_WRITE_DATA);
    struct uk_fd *fd = get_handle_fd_obj( current_thread->process, req->async.handle, access );

    if (fd)
    {
        reply->wait = fd->fd_ops->ioctl( fd, req->code, &req->async, req->blocking,
                                         get_req_data(), get_req_data_size() );
        reply->options = fd->options;
        release_object( fd );
    }
}

/* create / reschedule an async I/O */
DECL_HANDLER(register_async)
{
    unsigned int access;
    struct uk_fd *fd;

    switch(req->type)
    {
    case ASYNC_TYPE_READ:
        access = FILE_READ_DATA;
        break;
    case ASYNC_TYPE_WRITE:
        access = FILE_WRITE_DATA;
        break;
    default:
        set_error( STATUS_INVALID_PARAMETER );
        return;
    }

    if ((fd = get_handle_fd_obj( current_thread->process, req->async.handle, access )))
    {
        if (get_unix_fd( fd ) != -1) fd->fd_ops->queue_async( fd, &req->async, req->type, req->count );
        release_object( fd );
    }
}

/* cancels all async I/O */
DECL_HANDLER(cancel_async)
{
    struct uk_fd *fd = get_handle_fd_obj( current_thread->process, req->handle, 0 );
    struct thread *thread = req->only_thread ? current_thread : NULL;

    if (fd)
    {
        if (get_unix_fd( fd ) != -1) fd->fd_ops->cancel_async( fd, current_thread->process, thread, req->iosb );
        release_object( fd );
    }
}

/* attach completion object to a fd */
DECL_HANDLER(set_completion_info)
{
    struct uk_fd *fd = get_handle_fd_obj( current_thread->process, req->handle, 0 );

    if (fd)
    {
        if (!(fd->options & (FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT)) && !fd->completion)
        {
            fd->completion = get_completion_obj( current_thread->process, req->chandle, IO_COMPLETION_MODIFY_STATE );
            fd->comp_key = req->ckey;
        }
        else set_error( STATUS_INVALID_PARAMETER );
        release_object( fd );
    }
}

/* push new completion msg into a completion queue attached to the fd */
DECL_HANDLER(add_fd_completion)
{
    struct uk_fd *fd = get_handle_fd_obj( current_thread->process, req->handle, 0 );
    if (fd)
    {
        if (fd->completion)
            add_completion( fd->completion, fd->comp_key, req->cvalue, req->status, req->information );
        release_object( fd );
    }
}
