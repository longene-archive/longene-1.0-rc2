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

#ifndef _UK_ASSERT_H_
#define _UK_ASSERT_H_

//#define NDEBUG

#undef assert

#ifdef NDEBUG
#define assert(_expr) ((void)0)
#else
extern void _assert(const char *, const char *, unsigned int);
#define assert(_expr) (void)((!!(_expr)) || (_assert(#_expr, __FILE__, __LINE__), 0))
#endif

#endif
