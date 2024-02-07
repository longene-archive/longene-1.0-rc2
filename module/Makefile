
obj-m := unifiedkernel.o

unifiedkernel-objs := \
	async.o \
	atom.o \
	change.o \
	class.o \
	clipboard.o \
	completion.o \
	console.o \
	debugger.o \
	device.o \
	directory.o \
	event.o \
	fd.o \
	file.o \
	handle.o \
	hook.o \
	mach.o \
	mailslot.o \
	main.o \
	mapping.o \
	mutex.o \
	named_pipe.o \
	object.o \
	process.o \
	procfs.o \
	ptrace.o \
	queue.o \
	region.o \
	registry.o \
	request.o \
	semaphore.o \
	serial.o \
	signal.o \
	snapshot.o \
	sock.o \
	symlink.o \
	thread.o \
	timer.o \
	token.o \
	trace.o \
	unicode.o \
	user.o \
	window.o \
	winstation.o \
	lib.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	+make -Wall -C $(KDIR) M=$(PWD) EXTRA_CFLAGS="-Wno-unused-function -I$(PWD)/include -I$(PWD)/../wine/include -D CONFIG_UNIFIED_KERNEL -D CONFIG_FIX_REDEFINED" modules
	
clean:
	make -C $(KDIR) M=$(PWD) clean
