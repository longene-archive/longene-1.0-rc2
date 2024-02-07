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

#ifndef _UK_SYS_TIME_H
#define _UK_SYS_TIME_H

#include <linux/time.h>

#if BITS_PER_LONG == 32
extern long long get_current_time(void);
#elif BITS_PER_LONG == 64
extern long get_current_time(void);
#endif

#define current_time (get_current_time())

#endif
