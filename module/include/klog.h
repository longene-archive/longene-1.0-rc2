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

#ifndef _UK_KLOG_H_
#define _UK_KLOG_H_

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/smp.h>

#define KLOG

#ifdef KLOG
#define REFCNT(obj) ( (obj) ? (((struct object*)(obj))->refcount) : (0xffff) )
#define klog(trace,FMT...) \
	do { \
		printk("UK: (%s:%d) cpu %d p %d t %d %s ",strrchr(__FILE__,'/'),__LINE__, smp_processor_id(), current->tgid, current->pid, __FUNCTION__); \
		printk(FMT); \
		if(trace) dump_stack(); \
	} while (0)
#else
#define klog(FMT...) do { } while (0)
#endif

#endif
