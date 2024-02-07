/*
 * Server main function
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

#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_H
# include <getopt.h>
#endif

#include "object.h"
#include "file.h"
#include "thread.h"
#include "request.h"
#include "wine/library.h"

/* command-line options */
int debug_level = 0;
int foreground = 0;
timeout_t master_socket_timeout = 3 * -TICKS_PER_SEC;  /* master socket timeout, default is 3 seconds */
const char *server_argv0;

/* parse-line args */

static void usage( FILE *fh )
{
    fprintf(fh, "Usage: %s [options]\n\n", server_argv0);
    fprintf(fh, "Options:\n");
    fprintf(fh, "   -d[n], --debug[=n]       set debug level to n or +1 if n not specified\n");
    fprintf(fh, "   -f,    --foreground      remain in the foreground for debugging\n");
    fprintf(fh, "   -h,    --help            display this help message\n");
    fprintf(fh, "   -k[n], --kill[=n]        kill the current_thread wineserver, optionally with signal n\n");
    fprintf(fh, "   -p[n], --persistent[=n]  make server persistent, optionally for n seconds\n");
    fprintf(fh, "   -v,    --version         display version information and exit\n");
    fprintf(fh, "   -w,    --wait            wait until the current_thread wineserver terminates\n");
    fprintf(fh, "\n");
}

static void wine_parse_args( int argc, char *argv[] )
{
    int ret, optc;

    static struct option long_options[] =
    {
        {"debug",       2, NULL, 'd'},
        {"foreground",  0, NULL, 'f'},
        {"help",        0, NULL, 'h'},
        {"kill",        2, NULL, 'k'},
        {"persistent",  2, NULL, 'p'},
        {"version",     0, NULL, 'v'},
        {"wait",        0, NULL, 'w'},
        { NULL,         0, NULL, 0}
    };

    server_argv0 = argv[0];

    while ((optc = getopt_long( argc, argv, "d::fhk::p::vw", long_options, NULL )) != -1)
    {
        switch(optc)
        {
            case 'd':
                if (optarg && isdigit(*optarg))
                    debug_level = atoi( optarg );
                else
                    debug_level++;
                break;
            case 'f':
                foreground = 1;
                break;
            case 'h':
                usage(stdout);
                exit(0);
                break;
            case 'k':
                if (optarg && isdigit(*optarg))
                    ret = kill_lock_owner( atoi( optarg ) );
                else
                    ret = kill_lock_owner(-1);
                exit( !ret );
            case 'p':
                if (optarg && isdigit(*optarg))
                    master_socket_timeout = (timeout_t)atoi( optarg ) * -TICKS_PER_SEC;
                else
                    master_socket_timeout = TIMEOUT_INFINITE;
                break;
            case 'v':
                fprintf( stderr, "%s\n", wine_get_build_id());
                exit(0);
            case 'w':
                wait_for_lock();
                exit(0);
            default:
                usage(stderr);
                exit(1);
        }
    }
}

static void sigterm_handler( int signum )
{
    exit(1);  /* make sure atexit functions get called */
}

int main( int argc, char *argv[] )
{
    setvbuf( stderr, NULL, _IOLBF, 0 );
    wine_parse_args( argc, argv );

    /* setup temporary handlers before the real signal initialization is done */
    signal( SIGPIPE, SIG_IGN );
    signal( SIGHUP, sigterm_handler );
    signal( SIGINT, sigterm_handler );
    signal( SIGQUIT, sigterm_handler );
    signal( SIGTERM, sigterm_handler );
    signal( SIGABRT, sigterm_handler );

    sock_init();
    open_master_socket();

    if (debug_level) fprintf( stderr, "wineserver: starting (pid=%ld)\n", (long) getpid() );
    init_signals();
    init_directories();
    init_registry();
    main_loop();
    return 0;
}

#ifdef CONFIG_UNIFIED_KERNEL
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>

extern void init_thread_hash_table(void);
extern int create_syscall_chardev(void);
extern void destroy_syscall_chardev(void);
extern void get_kallsyms_lookup_name(void);
extern int timer_loop(void*);
extern void destroy_reg_name( void );
extern void register_pe_binfmt(void);
extern void unregister_pe_binfmt(void);

struct task_struct* timer_kernel_task = NULL;

/* module entry*/
static int __init unifiedkernel_init(void)
{
    server_start_time = current_time;
    get_kallsyms_lookup_name();
    init_thread_hash_table();
    create_syscall_chardev();
    init_directories();
    init_uk_lock();
    register_pe_binfmt();

    timer_kernel_task = kthread_run(timer_loop, NULL, "timer_thread");
    if(IS_ERR(timer_kernel_task))
    {
        klog(0, "create timer_thread failed \n");
    }

    return 0;
}

static void __exit unifiedkernel_exit(void)
{
    destroy_syscall_chardev();
    unregister_pe_binfmt();
    kthread_stop(timer_kernel_task);
    flush_registry();
#ifdef DEBUG_OBJECTS
    close_objects();  /* shut down everything properly */
#endif
    destroy_reg_name();
#ifdef MEM_LEAK_CHECK
    void print_mem_list(void);
    print_mem_list();
#endif
}

module_init(unifiedkernel_init);
module_exit(unifiedkernel_exit);

MODULE_AUTHOR("insigma");
MODULE_LICENSE("GPL");
#endif
