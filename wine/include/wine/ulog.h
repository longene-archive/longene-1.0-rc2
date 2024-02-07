#ifndef _LOG_H_
#define	_LOG_H_

#define _GNU_SOURCE
#define __USE_GNU

#include <stdio.h>
#include "winternl.h"
#include "winnt.h"

#define ULOG 1

#ifdef ULOG

#include <link.h> 	// for dynamic shared object
#include <execinfo.h>	//for backtrace
#include <dlfcn.h>	//for backtrace

#define __NR_gettid 224

#define	ulog(trace, fmt...) \
	do { \
		fprintf(stderr, "%s[%d] p %d t %d ", __FUNCTION__,__LINE__, getpid(), syscall(__NR_gettid)); \
		fprintf(stderr, fmt); \
		if (trace) \
		{ \
			unsigned long 	count;\
			unsigned long	*frame; \
			unsigned long	ebp, esp; \
			asm volatile ("movl %%esp, %%eax\nmovl %%ebp, %%edx" : "=&a"(esp), "=&d"(ebp)); \
			frame = (unsigned long *)ebp; \
			count = 0; \
			while (ebp && ebp > esp && ebp < esp + 0x10000) \
			{ \
				Dl_info dlinfo; \
				memset( &dlinfo, 0, sizeof(Dl_info)); \
				dladdr((void*)frame[1], &dlinfo); \
				fprintf(stderr, "\t[%02d] %08x : <%s+%p> \n",++count,frame[1], \
						dlinfo.dli_sname, (int*)frame[1]-(int*)(dlinfo.dli_saddr)); \
				ebp = *frame; \
				frame = (unsigned long *)ebp; \
			} \
		} \
	} while (0);


#define	ulog_server(trace, fmt...) \
	do { \
		fprintf(stderr, "%s[%d] p %d t %d ", __FUNCTION__,__LINE__, getpid(), syscall(__NR_gettid)); \
		fprintf(stderr, fmt); \
		if (trace) \
		{ \
			unsigned long 	count;\
			unsigned long	*frame; \
			unsigned long	ebp, esp; \
			asm volatile ("movl %%esp, %%eax\nmovl %%ebp, %%edx" : "=&a"(esp), "=&d"(ebp)); \
			frame = (unsigned long *)ebp; \
			count = 0;\
			while (ebp && ebp > esp && ebp < esp + 0x10000) \
			{ \
				fprintf(stderr, "\t[%02d] %08x \n",++count,frame[1]); \
				ebp = *frame; \
				frame = (unsigned long *)ebp; \
			} \
		} \
	} while (0)
#else
#define	ulog(trace, fmt...)  do { } while (0)
#define	ulog_server(trace, fmt...)  do { } while (0)
#endif


#define LOG_FILE	"/tmp/unified.trace"

#ifdef DEBUG_SYSCALL

static inline unsigned long long rdtsc()
{
	unsigned long long      ret;

	asm volatile ("rdtsc\n" : "=A"(ret));
	return ret >> 20;
}

#define LOG_NO_FUNC(file, fmt...) \
	do \
{ \
	FILE	*fp; \
	if ((fp = fopen((file), "a+"))) \
	{ \
		fprintf(fp, "%08llx: p %lx t %lx ", rdtsc(), \
				(unsigned long)getpid(), (unsigned long)gettid()); \
		fprintf(fp, fmt); \
		fclose(fp); \
	} \
	else \
	fprintf(stderr, "can not open file %s\n", (file)); \
} while (0)

#define	LOG(file, trace, status, fmt...) \
	do \
{ \
	FILE	*fp; \
	if ((fp = fopen((file), "a+"))) \
	{ \
		fprintf(fp, "%08llx: p %lx t %lx %s ", rdtsc(), \
				(unsigned long)getpid(), (unsigned long)gettid(), __FUNCTION__); \
		if (status) \
		fprintf(fp, "ERR: " fmt); \
		else \
		fprintf(fp, fmt); \
		if (trace) \
		{ \
			unsigned long	*frame; \
			unsigned long	ebp, esp; \
			asm volatile ("movl %%esp, %%eax\nmovl %%ebp, %%edx" : "=&a"(esp), "=&d"(ebp)); \
			frame = (unsigned long *)ebp; \
			fprintf(fp, "call trace:\n"); \
			while (ebp && ebp > esp && ebp < esp + 0x10000) \
			{ \
				fprintf(fp, "\treturn address 0x%lx\n", frame[1]); \
				ebp = *frame; \
				frame = (unsigned long *)ebp; \
			} \
		} \
		fclose(fp); \
	} \
	else \
	fprintf(stderr, "can not open file %s,%s\n", __FILE__, __FUNCTION__); \
} while (0)

#define   CALL_TRACE(trace, fmt...) \
	do \
{\
	printf("p %d t %d %s ",(unsigned long)getpid(), (unsigned long)gettid(), __FUNCTION__); \
	printf(fmt); \
	if (trace) \
	{ \
		unsigned long   *frame; \
		unsigned long   ebp, esp, i; \
		asm volatile ("movl %%esp, %%eax\nmovl %%ebp, %%edx" : "=&a"(esp), "=&d"(ebp)); \
		frame = (unsigned long *)ebp; \
		printf("call trace:\n"); \
		while (i++,ebp && ebp > esp && ebp < esp + 0x10000) \
		{ \
			printf("\t[%02d] 0x%08x\n",i, frame[1]); \
			ebp = *frame; \
			frame = (unsigned long *)ebp; \
		} \
	} \
} while (0)
#else

#define	LOG(file, trace, status, fmt...)	do { } while (0)
#define LOG_NO_FUNC(file, fmt...)	 	do { } while (0)
#define LOG_SIMPLIFY_START(file, trace, fmt...)	do { } while (0)
#define LOG_SIMPLIFY_END(file, status, fmt...) 	do { } while (0)

#endif	/* DEBUG_SYSCALL */

#endif
