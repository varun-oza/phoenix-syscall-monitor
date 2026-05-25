#ifndef SYSCALL_MONITOR_H
#define SYSCALL_MONITOR_H

#include <linux/ioctl.h>

/* The three modes */
#define MODE_OFF   0
#define MODE_LOG   1
#define MODE_BLOCK 2

/* Which syscall to target */
#define SYSCALL_OPEN  0
#define SYSCALL_READ  1
#define SYSCALL_WRITE 2

/* Magic number for ioctl - identifies our device */
#define MONITOR_MAGIC 0xBB

/* ioctl commands */
#define IOCTL_SET_MODE    _IOW(MONITOR_MAGIC, 0, int)
#define IOCTL_SET_PID     _IOW(MONITOR_MAGIC, 1, int)
#define IOCTL_SET_SYSCALL _IOW(MONITOR_MAGIC, 2, int)
#define IOCTL_GET_COUNT   _IOR(MONITOR_MAGIC, 3, int)

/* Device name - will appear as /dev/syscall_monitor */
#define DEVICE_NAME "syscall_monitor"

#endif
