/*
 * Server-side IO completion ports implementation
 *
 * Copyright (C) 2007 Andrey Turkin
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
 *
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

/* FIXMEs:
 *  - built-in wait queues used which means:
 *    + threads are awaken FIFO and not LIFO as native does
 *    + "max concurrent active threads" parameter not used
 *    + completion handle is waitable, while native isn't
 */

#include "config.h"
#include "wine/port.h"

#include <stdarg.h>
#include <stdio.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"

#include "wine/unicode.h"
#include "object.h"
#include "file.h"
#include "handle.h"
#include "request.h"


struct uk_completion
{
    struct object  obj;
    struct list_head    queue;
    unsigned int   depth;
};

static void completion_dump( struct object*, int );
static struct object_type *completion_get_type( struct object *obj );
static int completion_signaled( struct object *obj, struct wait_queue_entry *entry );
static unsigned int completion_map_access( struct object *obj, unsigned int access );
static void completion_destroy( struct object * );

static const struct object_ops completion_ops =
{
    sizeof(struct uk_completion), /* size */
    completion_dump,           /* dump */
    completion_get_type,       /* get_type */
    add_queue,                 /* add_queue */
    remove_queue,              /* remove_queue */
    completion_signaled,       /* signaled */
    no_satisfied,              /* satisfied */
    no_signal,                 /* signal */
    no_get_fd,                 /* get_fd */
    completion_map_access,     /* map_access */
    default_get_sd,            /* get_sd */
    default_set_sd,            /* set_sd */
    no_lookup_name,            /* lookup_name */
    no_open_file,              /* open_file */
    no_close_handle,           /* close_handle */
    completion_destroy         /* destroy */
};

struct comp_msg
{
    struct   list_head queue_entry;
    apc_param_t   ckey;
    apc_param_t   cvalue;
    apc_param_t   information;
    unsigned int  status;
};

static void completion_destroy( struct object *obj)
{
    struct uk_completion *completion = (struct uk_completion *) obj;
    struct comp_msg *tmp, *next;

    LIST_FOR_EACH_ENTRY_SAFE( tmp, next, &completion->queue, struct comp_msg, queue_entry )
    {
        free( tmp );
    }
}

static void completion_dump( struct object *obj, int verbose )
{
    struct uk_completion *completion = (struct uk_completion *) obj;

    assert( obj->ops == &completion_ops );
    fprintf( stderr, "Completion " );
    dump_object_name( &completion->obj );
    fprintf( stderr, " (%u packets pending)\n", completion->depth );
}

static struct object_type *completion_get_type( struct object *obj )
{
    static const WCHAR name[] = {'C','o','m','p','l','e','t','i','o','n'};
    static const struct unicode_str str = { name, sizeof(name) };
    return get_object_type( &str );
}

static int completion_signaled( struct object *obj, struct wait_queue_entry *entry )
{
    struct uk_completion *completion = (struct uk_completion *)obj;

    return !list_empty( &completion->queue );
}

static unsigned int completion_map_access( struct object *obj, unsigned int access )
{
    if (access & GENERIC_READ)    access |= STANDARD_RIGHTS_READ | SYNCHRONIZE | IO_COMPLETION_QUERY_STATE;
    if (access & GENERIC_WRITE)   access |= STANDARD_RIGHTS_WRITE;
    if (access & GENERIC_EXECUTE) access |= STANDARD_RIGHTS_EXECUTE;
    if (access & GENERIC_ALL)     access |= STANDARD_RIGHTS_ALL | IO_COMPLETION_ALL_ACCESS;
    return access & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

static struct uk_completion *create_completion( struct directory *root, const struct unicode_str *name, unsigned int attr, unsigned int concurrent )
{
    struct uk_completion *completion;

    if ((completion = create_named_object_dir( root, name, attr, &completion_ops )))
    {
        if (get_error() != STATUS_OBJECT_NAME_EXISTS)
        {
            list_init( &completion->queue );
            completion->depth = 0;
        }
    }

    return completion;
}

struct uk_completion *get_completion_obj( struct process *process, obj_handle_t handle, unsigned int access )
{
    return (struct uk_completion *) get_handle_obj( process, handle, access, &completion_ops );
}

void add_completion( struct uk_completion *completion, apc_param_t ckey, apc_param_t cvalue,
                     unsigned int status, apc_param_t information )
{
    struct comp_msg *msg = mem_alloc( sizeof( *msg ) );

    if (!msg)
        return;

    msg->ckey = ckey;
    msg->cvalue = cvalue;
    msg->status = status;
    msg->information = information;

    wine_list_add_tail( &completion->queue, &msg->queue_entry );
    completion->depth++;
    uk_wake_up( &completion->obj, 1 );
}

/* create a completion */
DECL_HANDLER(create_completion)
{
    struct uk_completion *completion;
    struct unicode_str name;
    struct directory *root = NULL;

    reply->handle = 0;

    get_req_unicode_str( &name );
    if (req->rootdir && !(root = get_directory_obj( current_thread->process, req->rootdir, 0 )))
        return;

    if ( (completion = create_completion( root, &name, req->attributes, req->concurrent )) != NULL )
    {
        reply->handle = alloc_handle( current_thread->process, completion, req->access, req->attributes );
        release_object( completion );
    }

    if (root) release_object( root );
}

/* open a completion */
DECL_HANDLER(open_completion)
{
    struct uk_completion *completion;
    struct unicode_str name;
    struct directory *root = NULL;

    reply->handle = 0;

    get_req_unicode_str( &name );
    if (req->rootdir && !(root = get_directory_obj( current_thread->process, req->rootdir, 0 )))
        return;

    if ( (completion = open_object_dir( root, &name, req->attributes, &completion_ops )) != NULL )
    {
        reply->handle = alloc_handle( current_thread->process, completion, req->access, req->attributes );
        release_object( completion );
    }

    if (root) release_object( root );
}


/* add completion to completion port */
DECL_HANDLER(add_completion)
{
    struct uk_completion* completion = get_completion_obj( current_thread->process, req->handle, IO_COMPLETION_MODIFY_STATE );

    if (!completion) return;

    add_completion( completion, req->ckey, req->cvalue, req->status, req->information );

    release_object( completion );
}

/* get completion from completion port */
DECL_HANDLER(remove_completion)
{
    struct uk_completion* completion = get_completion_obj( current_thread->process, req->handle, IO_COMPLETION_MODIFY_STATE );
    struct list_head *entry;
    struct comp_msg *msg;

    if (!completion) return;

    entry = list_head( &completion->queue );
    if (!entry)
        set_error( STATUS_PENDING );
    else
    {
        list_remove( entry );
        completion->depth--;
        msg = LIST_ENTRY( entry, struct comp_msg, queue_entry );
        reply->ckey = msg->ckey;
        reply->cvalue = msg->cvalue;
        reply->status = msg->status;
        reply->information = msg->information;
        free( msg );
    }

    release_object( completion );
}

/* get queue depth for completion port */
DECL_HANDLER(query_completion)
{
    struct uk_completion* completion = get_completion_obj( current_thread->process, req->handle, IO_COMPLETION_QUERY_STATE );

    if (!completion) return;

    reply->depth = completion->depth;

    release_object( completion );
}