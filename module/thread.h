/*
 * Wine server threads
 *
 * Copyright (C) 1998 Alexandre Julliard
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

#ifndef __WINE_SERVER_THREAD_H
#define __WINE_SERVER_THREAD_H

#include "object.h"
#ifdef CONFIG_UNIFIED_KERNEL
#include "wine/list.h"
#include <linux/completion.h>
#include <wine/server.h>
#endif

/* thread structure */

struct process;
struct thread_wait;
struct thread_apc;
struct debug_ctx;
struct debug_event;
struct msg_queue;

enum run_state
{
    RUNNING,    /* running normally */
    TERMINATED  /* terminated */
};

/* descriptor for fds currently in flight from client to server */
struct inflight_fd
{
    int client;  /* fd on the client side (or -1 if entry is free) */
    int server;  /* fd on the server side */
};
#define MAX_INFLIGHT_FDS 16  /* max number of fds in flight per thread */

struct thread
{
    struct object          obj;           /* object header */
    struct list_head            entry;         /* entry in system-wide thread list */
    struct list_head            proc_entry;    /* entry in per-process thread list */
    struct process        *process;
    thread_id_t            id;            /* thread id */
#ifdef CONFIG_UNIFIED_KERNEL
    pid_t                  pid; /* for find_thread_by_pid()*/
    struct hlist_node      hash_entry;
    int                    unix_errno; /* for global errno macro */
    struct completion      completion;
    struct wake_up_reply   wake_info;
#endif
    struct list_head            mutex_list;    /* list of currently owned mutexes */
    struct debug_ctx      *debug_ctx;     /* debugger context if this thread is a debugger */
    struct debug_event    *debug_event;   /* debug event being sent to debugger */
    int                    debug_break;   /* debug breakpoint pending? */
    struct msg_queue      *queue;         /* message queue */
    struct thread_wait    *wait;          /* current_thread wait condition if sleeping */
    struct list_head            system_apc;    /* queue of system async procedure calls */
    struct list_head            user_apc;      /* queue of user async procedure calls */
    struct inflight_fd     inflight[MAX_INFLIGHT_FDS];  /* fds currently in flight */
    unsigned int           error;         /* current_thread error code */
    union generic_request  req;           /* current_thread request */
    void                  *req_data;      /* variable-size data for request */
    unsigned int           req_toread;    /* amount of data still to read in request */
    void                  *reply_data;    /* variable-size data for reply */
    unsigned int           reply_size;    /* size of reply data */
    unsigned int           reply_towrite; /* amount of data still to write in reply */
    struct uk_fd             *request_fd;    /* fd for receiving client requests */
    struct uk_fd             *reply_fd;      /* fd to send a reply to a client */
    struct uk_fd             *wait_fd;       /* fd to use to wake a sleeping client */
    enum run_state         state;         /* running state */
    int                    exit_code;     /* thread exit code */
    int                    unix_pid;      /* Unix pid of client */
    int                    unix_tid;      /* Unix tid of client */
    context_t             *context;       /* current_thread context if in an exception handler */
    context_t             *suspend_context; /* current_thread context if suspended */
    client_ptr_t           teb;           /* TEB address (in client address space) */
    affinity_t             affinity;      /* affinity mask */
    int                    priority;      /* priority level */
    int                    suspend;       /* suspend count */
    obj_handle_t           desktop;       /* desktop handle */
    int                    desktop_users; /* number of objects using the thread desktop */
    timeout_t              creation_time; /* Thread creation time */
    timeout_t              exit_time;     /* Thread exit time */
    struct token          *token;         /* security token associated with this thread */
};

struct thread_snapshot
{
    struct thread  *thread;    /* thread ptr */
    int             count;     /* thread refcount */
    int             priority;  /* priority class */
};

//extern struct thread *current_thread;

/* thread functions */

#ifdef CONFIG_UNIFIED_KERNEL
extern void add_thread_by_pid(struct thread *thread, pid_t pid);
extern struct thread* get_thread_by_task(struct task_struct *task);
extern struct thread* get_current_thread(void);
#define current_thread get_current_thread()
#endif
extern struct thread *create_thread( int fd, struct process *process );
extern struct thread *get_thread_from_id( thread_id_t id );
extern struct thread *get_thread_from_handle( obj_handle_t handle, unsigned int access );
extern struct thread *get_thread_from_tid( int tid );
extern struct thread *get_thread_from_pid( int pid );
extern struct thread *get_wait_queue_thread( struct wait_queue_entry *entry );
extern enum select_op get_wait_queue_select_op( struct wait_queue_entry *entry );
extern client_ptr_t get_wait_queue_key( struct wait_queue_entry *entry );
extern void make_wait_abandoned( struct wait_queue_entry *entry );
extern void stop_thread( struct thread *thread );
extern void stop_thread_if_suspended( struct thread *thread );
extern int wake_thread( struct thread *thread );
extern int wake_thread_queue_entry( struct wait_queue_entry *entry );
extern int add_queue( struct object *obj, struct wait_queue_entry *entry );
extern void remove_queue( struct object *obj, struct wait_queue_entry *entry );
extern void kill_thread( struct thread *thread, int violent_death );
extern void break_thread( struct thread *thread );
extern void uk_wake_up( struct object *obj, int max );
extern int thread_queue_apc( struct thread *thread, struct object *owner, const apc_call_t *call_data );
extern void thread_cancel_apc( struct thread *thread, struct object *owner, enum apc_type type );
extern int thread_add_inflight_fd( struct thread *thread, int client, int server );
extern int thread_get_inflight_fd( struct thread *thread, int client );
extern struct thread_snapshot *thread_snap( int *count );
extern struct token *thread_get_impersonation_token( struct thread *thread );
extern int set_thread_affinity( struct thread *thread, affinity_t affinity );
extern int is_cpu_supported( enum cpu_type cpu );

/* ptrace functions */

extern void sigchld_callback(void);
extern void get_thread_context( struct thread *thread, context_t *context, unsigned int flags );
extern void set_thread_context( struct thread *thread, const context_t *context, unsigned int flags );
extern int send_thread_signal( struct thread *thread, int sig );
extern void get_selector_entry( struct thread *thread, int entry, unsigned int *base,
                                unsigned int *limit, unsigned char *flags );

extern unsigned int global_error;  /* global error code for when no thread is current_thread */

static inline unsigned int get_error(void)       { return current_thread ? current_thread->error : global_error; }
static inline void set_error( unsigned int err ) { global_error = err; if (current_thread) current_thread->error = err; }
static inline void clear_error(void)             { set_error(0); }
static inline void set_win32_error( unsigned int err ) { set_error( 0xc0010000 | err ); }

static inline thread_id_t get_thread_id( struct thread *thread ) { return thread->id; }

#endif  /* __WINE_SERVER_THREAD_H */
