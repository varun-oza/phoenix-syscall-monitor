obj-m += syscall_monitor.o
KDIR := /lib/modules/$(shell uname -r)/build
all:
	$(MAKE) -C $(KDIR) M=$(PWD) CC=gcc-12 modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
load:
	sudo insmod syscall_monitor.ko
	sudo chmod 666 /dev/syscall_monitor
unload:
	sudo rmmod syscall_monitor
