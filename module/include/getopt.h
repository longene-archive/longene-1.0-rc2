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

#ifndef _UK_GETOPT_H_
#define _UK_GETOPT_H_

extern char *optarg;
extern int optind, opterr, optopt;

struct option {
    const char *name;
    int         has_arg;
    int        *flag;
    int         val;
};
int getopt(int argc, char * const argv[], const char *optstring);
int getopt_long(int argc, char * const argv[],
           const char *optstring,
           const struct option *longopts, int *longindex);
           
#endif
