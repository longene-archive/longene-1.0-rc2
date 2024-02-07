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

#ifndef _UK_STDIO_H_
#define _UK_STDIO_H_

#include <linux/types.h>
#include "stdarg.h"

#define _IOFBF    0x0000
#define _IONBF    0x0004
#define _IOLBF    0x0040

#define EOF       (-1)

#define BUFSIZ    512

#ifndef SEEK_SET
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2
#endif

typedef struct _iobuf
{
    struct file *filp;
    loff_t pos;
    char *buf;
    long buflen;
    long bufpos;
    ssize_t validlen;
} FILE;


//#define stdin   ((FILE*)0) /*define in lib.c*/
#define stdout  ((FILE*)1)
#define stderr  ((FILE*)2)


void    clearerr(FILE*);
int     fclose(FILE*);
int     feof(FILE*);
int     ferror(FILE*);
int     fflush(FILE*);
int     fgetc(FILE*);
char*   fgets(char*,int,FILE*);
FILE*   fopen(const char*,const char*);
FILE *  fopen(const char *path, const char *mode);
FILE *  fdopen(int fd, const char *mode);

int     fprintf(FILE*,const char*,...);
int     fputc(int,FILE*);
int     fputs(const char*,FILE*);
size_t  fread(void*,size_t,size_t,FILE*);
FILE*   freopen(const char*,const char*,FILE*);
int     fscanf(FILE*,const char*,...);
int     fscanf_s(FILE*,const char*,...);
int     fseek(FILE*,long,int);
long    ftell(FILE*);
size_t  fwrite(const void*,size_t,size_t,FILE*);
int     getc(FILE*);
int     getchar(void);
char*   gets(char*);
void    perror(const char*);
int     printf(const char*,...);
int     putc(int,FILE*);
int     putchar(int);
int     puts(const char*);
int     rename(const char*,const char*);
int     scanf(const char*,...);
void    setbuf(FILE*,char*);
int     setvbuf(FILE*,char*,int,size_t);
int     sprintf(char*,const char*,...);
int     sscanf(const char*,const char*,...);
int     vfprintf(FILE*,const char*,va_list);
int     vprintf(const char*,va_list);
int     vsprintf(char*,const char*,va_list);

#endif
