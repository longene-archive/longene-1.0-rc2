/*
 * Server-side file definitions
 *
 * Copyright (C) 2003 Alexandre Julliard
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

#ifndef __WINE_SERVER_FILE_H
#define __WINE_SERVER_FILE_H

#include "object.h"

struct uk_fd;
struct mapping;
struct async_queue;
struct uk_completion;

/* operations valid on file descriptor objects */
struct fd_ops
{
    /* get the events we want to poll() for on this object */
    int  (*get_poll_events)(struct uk_fd *);
    /* a poll() event occurred */
    void (*poll_event)(struct uk_fd *,int event);
    /* flush the object buffers */
    void (*flush)(struct uk_fd *, struct event **);
    /* get file information */
    enum server_fd_type (*get_fd_type)(struct uk_fd *fd);
    /* perform an ioctl on the file */
    obj_handle_t (*ioctl)(struct uk_fd *fd, ioctl_code_t code, const async_data_t *async, int blocking,
                          const void *data, data_size_t size);
    /* queue an async operation */
    void (*queue_async)(struct uk_fd *, const async_data_t *data, int type, int count);
    /* selected events for async i/o need an update */
    void (*reselect_async)( struct uk_fd *, struct async_queue *queue );
    /* cancel an async operation */
    void (*cancel_async)(struct uk_fd *, struct process *process, struct thread *thread, client_ptr_t iosb);
};

/* file descriptor functions */

extern struct uk_fd *alloc_pseudo_fd( const struct fd_ops *fd_user_ops, struct object *user,
                                   unsigned int options );
extern void set_no_fd_status( struct uk_fd *fd, unsigned int status );
extern struct uk_fd *open_fd( struct uk_fd *root, const char *name, int flags, mode_t *mode,
                           unsigned int access, unsigned int sharing, unsigned int options );
extern struct uk_fd *create_anonymous_fd( const struct fd_ops *fd_user_ops,
                                       int unix_fd, struct object *user, unsigned int options );
extern struct uk_fd *dup_fd_object( struct uk_fd *orig, unsigned int access, unsigned int sharing,
                                 unsigned int options );
extern struct uk_fd *get_fd_object_for_mapping( struct uk_fd *fd, unsigned int access, unsigned int sharing );
extern void *get_fd_user( struct uk_fd *fd );
extern void set_fd_user( struct uk_fd *fd, const struct fd_ops *ops, struct object *user );
extern unsigned int get_fd_options( struct uk_fd *fd );
extern int get_unix_fd( struct uk_fd *fd );
extern int is_same_file_fd( struct uk_fd *fd1, struct uk_fd *fd2 );
extern int is_fd_removable( struct uk_fd *fd );
extern int fd_close_handle( struct object *obj, struct process *process, obj_handle_t handle );
extern int check_fd_events( struct uk_fd *fd, int events );
extern void set_fd_events( struct uk_fd *fd, int events );
extern obj_handle_t lock_fd( struct uk_fd *fd, file_pos_t offset, file_pos_t count, int shared, int wait );
extern void unlock_fd( struct uk_fd *fd, file_pos_t offset, file_pos_t count );
extern void allow_fd_caching( struct uk_fd *fd );
extern void set_fd_signaled( struct uk_fd *fd, int signaled );
extern int is_fd_signaled( struct uk_fd *fd );

extern int default_fd_signaled( struct object *obj, struct wait_queue_entry *entry );
extern unsigned int default_fd_map_access( struct object *obj, unsigned int access );
extern int default_fd_get_poll_events( struct uk_fd *fd );
extern void default_poll_event( struct uk_fd *fd, int event );
extern struct async *fd_queue_async( struct uk_fd *fd, const async_data_t *data, int type );
extern void fd_async_wake_up( struct uk_fd *fd, int type, unsigned int status );
extern void fd_reselect_async( struct uk_fd *fd, struct async_queue *queue );
extern obj_handle_t no_fd_ioctl( struct uk_fd *fd, ioctl_code_t code, const async_data_t *async,
                                 int blocking, const void *data, data_size_t size );
extern obj_handle_t default_fd_ioctl( struct uk_fd *fd, ioctl_code_t code, const async_data_t *async,
                                      int blocking, const void *data, data_size_t size );
extern void no_fd_queue_async( struct uk_fd *fd, const async_data_t *data, int type, int count );
extern void default_fd_queue_async( struct uk_fd *fd, const async_data_t *data, int type, int count );
extern void default_fd_reselect_async( struct uk_fd *fd, struct async_queue *queue );
extern void default_fd_cancel_async( struct uk_fd *fd, struct process *process, struct thread *thread, client_ptr_t iosb );
extern void no_flush( struct uk_fd *fd, struct event **event );
extern void main_loop(void);
extern void remove_process_locks( struct process *process );

static inline struct uk_fd *get_obj_fd( struct object *obj ) { return obj->ops->get_fd( obj ); }

/* timeout functions */

struct timeout_user;
#ifndef CONFIG_UNIFIED_KERNEL
extern timeout_t current_time;
#endif

#define TICKS_PER_SEC 10000000

typedef void (*timeout_callback)( void *private );

extern struct timeout_user *add_timeout_user( timeout_t when, timeout_callback func, void *private );
extern void remove_timeout_user( struct timeout_user *user );
extern const char *get_timeout_str( timeout_t timeout );

/* file functions */

extern struct uk_file *get_file_obj( struct process *process, obj_handle_t handle,
                                  unsigned int access );
extern int get_file_unix_fd( struct uk_file *file );
extern int is_same_file( struct uk_file *file1, struct uk_file *file2 );
extern struct uk_file *create_file_for_fd( int fd, unsigned int access, unsigned int sharing );
extern struct uk_file *create_file_for_fd_obj( struct uk_fd *fd, unsigned int access, unsigned int sharing );
extern void file_set_error(void);
extern struct security_descriptor *mode_to_sd( mode_t mode, const SID *user, const SID *group );
extern mode_t sd_to_mode( const struct security_descriptor *sd, const SID *owner );

/* file mapping functions */

extern struct mapping *get_mapping_obj( struct process *process, obj_handle_t handle,
                                        unsigned int access );
extern obj_handle_t open_mapping_file( struct process *process, struct mapping *mapping,
                                       unsigned int access, unsigned int sharing );
extern struct mapping *grab_mapping_unless_removable( struct mapping *mapping );
extern int get_page_size(void);

/* change notification functions */

extern void do_change_notify( int unix_fd );
extern void sigio_callback(void);
extern struct object *create_dir_obj( struct uk_fd *fd, unsigned int access, mode_t mode );
extern struct dir *get_dir_obj( struct process *process, obj_handle_t handle, unsigned int access );

/* completion */

extern struct uk_completion *get_completion_obj( struct process *process, obj_handle_t handle, unsigned int access );
extern void add_completion( struct uk_completion *completion, apc_param_t ckey, apc_param_t cvalue,
                            unsigned int status, apc_param_t information );

/* serial port functions */

extern int is_serial_fd( struct uk_fd *fd );
extern struct object *create_serial( struct uk_fd *fd );

/* async I/O functions */
extern struct async_queue *create_async_queue( struct uk_fd *fd );
extern void free_async_queue( struct async_queue *queue );
extern struct async *create_async( struct thread *thread, struct async_queue *queue,
                                   const async_data_t *data );
extern void async_set_timeout( struct async *async, timeout_t timeout, unsigned int status );
extern void async_set_result( struct object *obj, unsigned int status,
                              apc_param_t total, client_ptr_t apc );
extern int async_queued( struct async_queue *queue );
extern int async_waiting( struct async_queue *queue );
extern void async_terminate( struct async *async, unsigned int status );
extern int async_wake_up_by( struct async_queue *queue, struct process *process,
                             struct thread *thread, client_ptr_t iosb, unsigned int status );
extern void async_wake_up( struct async_queue *queue, unsigned int status );
extern struct uk_completion *fd_get_completion( struct uk_fd *fd, apc_param_t *p_key );
extern void fd_copy_completion( struct uk_fd *src, struct uk_fd *dst );

/* access rights that require Unix read permission */
#define FILE_UNIX_READ_ACCESS (FILE_READ_DATA|FILE_READ_ATTRIBUTES|FILE_READ_EA)

/* access rights that require Unix write permission */
#define FILE_UNIX_WRITE_ACCESS (FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_ATTRIBUTES|FILE_WRITE_EA)

/* magic file access rights for mappings */
#define FILE_MAPPING_IMAGE  0x80000000  /* set for SEC_IMAGE mappings */
#define FILE_MAPPING_WRITE  0x40000000  /* set for writable shared mappings */
#define FILE_MAPPING_ACCESS 0x20000000  /* set for all mappings */

#endif  /* __WINE_SERVER_FILE_H */
