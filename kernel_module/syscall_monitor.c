#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include "syscall_monitor.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Varun Oza");
MODULE_DESCRIPTION("Syscall monitor - log/block/off modes via kprobes");
MODULE_VERSION("1.0");

/* ─── Global state ─────────────────────────────────────────────────────
 * These three variables control what the module does.
 * Protected by a spinlock so two CPUs can't change them simultaneously.
 * ────────────────────────────────────────────────────────────────────── */
static int current_mode   = MODE_OFF;
static int target_pid     = -1;
static int target_syscall = SYSCALL_OPEN;
static DEFINE_SPINLOCK(state_lock);

/* Counter: how many times the target syscall was seen
 * Userspace reads this to drive the FSM */
static atomic_t syscall_count = ATOMIC_INIT(0);

/* Character device variables */
static int            major_number;
static struct class  *monitor_class;
static struct device *monitor_device;

/* ─── Helper: log a syscall event ─────────────────────────────────────
 * Prints to kernel log (read with dmesg)
 * ────────────────────────────────────────────────────────────────────── */
static void log_syscall(const char *name, unsigned long arg0,
                        unsigned long arg1, unsigned long arg2)
{
    printk(KERN_INFO "[monitor] SYSCALL=%s PID=%d COMM=%s "
           "arg0=0x%lx arg1=0x%lx arg2=0x%lx\n",
           name, current->pid, current->comm,
           arg0, arg1, arg2);
}

/* ─── kprobe handler for open() ────────────────────────────────────────
 * Runs before every open() syscall on the whole system
 * ────────────────────────────────────────────────────────────────────── */
static int handler_pre_open(struct kprobe *p, struct pt_regs *regs)
{
    int mode, t_pid, t_sc;
    unsigned long flags;

    /* Read current settings safely */
    spin_lock_irqsave(&state_lock, flags);
    mode  = current_mode;
    t_pid = target_pid;
    t_sc  = target_syscall;
    spin_unlock_irqrestore(&state_lock, flags);

    /* OFF mode: do nothing */
    if (mode == MODE_OFF)
        return 0;

    /* LOG mode: write to kernel log */
    if (mode == MODE_LOG) {
        log_syscall("open", regs->di, regs->si, regs->dx);
        if (t_sc == SYSCALL_OPEN)
            atomic_inc(&syscall_count);
        return 0;
    }

    /* BLOCK mode: block open() for the target PID only */
    if (mode == MODE_BLOCK && t_sc == SYSCALL_OPEN
        && current->pid == t_pid) {
        printk(KERN_INFO "[monitor] BLOCKED open() for PID=%d\n",
               current->pid);
        regs->ax = (unsigned long)(-EPERM);
        return 1; /* 1 = skip the real syscall */
    }

    return 0;
}

/* ─── kprobe handler for read() ─────────────────────────────────────── */
static int handler_pre_read(struct kprobe *p, struct pt_regs *regs)
{
    int mode, t_pid, t_sc;
    unsigned long flags;

    spin_lock_irqsave(&state_lock, flags);
    mode  = current_mode;
    t_pid = target_pid;
    t_sc  = target_syscall;
    spin_unlock_irqrestore(&state_lock, flags);

    if (mode == MODE_OFF) return 0;

    if (mode == MODE_LOG) {
        log_syscall("read", regs->di, regs->si, regs->dx);
        if (t_sc == SYSCALL_READ)
            atomic_inc(&syscall_count);
        return 0;
    }

    if (mode == MODE_BLOCK && t_sc == SYSCALL_READ
        && current->pid == t_pid) {
        printk(KERN_INFO "[monitor] BLOCKED read() for PID=%d\n",
               current->pid);
        regs->ax = (unsigned long)(-EPERM);
        return 1;
    }

    return 0;
}

/* ─── kprobe handler for write() ────────────────────────────────────── */
static int handler_pre_write(struct kprobe *p, struct pt_regs *regs)
{
    int mode, t_pid, t_sc;
    unsigned long flags;

    spin_lock_irqsave(&state_lock, flags);
    mode  = current_mode;
    t_pid = target_pid;
    t_sc  = target_syscall;
    spin_unlock_irqrestore(&state_lock, flags);

    if (mode == MODE_OFF) return 0;

    if (mode == MODE_LOG) {
        log_syscall("write", regs->di, regs->si, regs->dx);
        if (t_sc == SYSCALL_WRITE)
            atomic_inc(&syscall_count);
        return 0;
    }

    if (mode == MODE_BLOCK && t_sc == SYSCALL_WRITE
        && current->pid == t_pid) {
        printk(KERN_INFO "[monitor] BLOCKED write() for PID=%d\n",
               current->pid);
        regs->ax = (unsigned long)(-EPERM);
        return 1;
    }

    return 0;
}

/* ─── kprobe structs ────────────────────────────────────────────────── */
static struct kprobe kp_open = {
    .symbol_name = "__x64_sys_openat",
    .pre_handler = handler_pre_open,
};
static struct kprobe kp_read = {
    .symbol_name = "__x64_sys_read",
    .pre_handler = handler_pre_read,
};
static struct kprobe kp_write = {
    .symbol_name = "__x64_sys_write",
    .pre_handler = handler_pre_write,
};

/* ─── ioctl handler ─────────────────────────────────────────────────────
 * Runs when userspace calls ioctl() on /dev/syscall_monitor
 * ────────────────────────────────────────────────────────────────────── */
static long monitor_ioctl(struct file *file, unsigned int cmd,
                          unsigned long arg)
{
    int value;
    unsigned long flags;
    int count;

    switch (cmd) {

    case IOCTL_SET_MODE:
        if (copy_from_user(&value, (int __user *)arg, sizeof(int)))
            return -EFAULT;
        if (value < MODE_OFF || value > MODE_BLOCK)
            return -EINVAL;
        spin_lock_irqsave(&state_lock, flags);
        current_mode = value;
        atomic_set(&syscall_count, 0);
        spin_unlock_irqrestore(&state_lock, flags);
        printk(KERN_INFO "[monitor] Mode set to %s\n",
               value == MODE_OFF ? "OFF" :
               value == MODE_LOG ? "LOG" : "BLOCK");
        break;

    case IOCTL_SET_PID:
        if (copy_from_user(&value, (int __user *)arg, sizeof(int)))
            return -EFAULT;
        spin_lock_irqsave(&state_lock, flags);
        target_pid = value;
        spin_unlock_irqrestore(&state_lock, flags);
        printk(KERN_INFO "[monitor] Target PID set to %d\n", value);
        break;

    case IOCTL_SET_SYSCALL:
        if (copy_from_user(&value, (int __user *)arg, sizeof(int)))
            return -EFAULT;
        if (value < SYSCALL_OPEN || value > SYSCALL_WRITE)
            return -EINVAL;
        spin_lock_irqsave(&state_lock, flags);
        target_syscall = value;
        atomic_set(&syscall_count, 0);
        spin_unlock_irqrestore(&state_lock, flags);
        printk(KERN_INFO "[monitor] Target syscall set to %s\n",
               value == SYSCALL_OPEN  ? "open"  :
               value == SYSCALL_READ  ? "read"  : "write");
        break;

    case IOCTL_GET_COUNT:
        count = atomic_read(&syscall_count);
        if (copy_to_user((int __user *)arg, &count, sizeof(int)))
            return -EFAULT;
        break;

    default:
        return -ENOTTY;
    }

    return 0;
}

static const struct file_operations monitor_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = monitor_ioctl,
};

/* ─── Module init ───────────────────────────────────────────────────── */
static int __init monitor_init(void)
{
    int ret;

    /* Step 1: Register character device */
    major_number = register_chrdev(0, DEVICE_NAME, &monitor_fops);
    if (major_number < 0) {
        printk(KERN_ERR "[monitor] register_chrdev failed: %d\n",
               major_number);
        return major_number;
    }

    /* Step 2: Create device class */
    monitor_class = class_create(DEVICE_NAME);
    if (IS_ERR(monitor_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(monitor_class);
    }

    /* Step 3: Create /dev/syscall_monitor */
    monitor_device = device_create(monitor_class, NULL,
                                   MKDEV(major_number, 0),
                                   NULL, DEVICE_NAME);
    if (IS_ERR(monitor_device)) {
        class_destroy(monitor_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(monitor_device);
    }

    /* Step 4: Register kprobes */
    ret = register_kprobe(&kp_open);
    if (ret < 0) {
        printk(KERN_ERR "[monitor] kprobe open failed: %d\n", ret);
        goto cleanup;
    }
    ret = register_kprobe(&kp_read);
    if (ret < 0) {
        printk(KERN_ERR "[monitor] kprobe read failed: %d\n", ret);
        unregister_kprobe(&kp_open);
        goto cleanup;
    }
    ret = register_kprobe(&kp_write);
    if (ret < 0) {
        printk(KERN_ERR "[monitor] kprobe write failed: %d\n", ret);
        unregister_kprobe(&kp_open);
        unregister_kprobe(&kp_read);
        goto cleanup;
    }

    printk(KERN_INFO "[monitor] Module loaded. /dev/%s ready.\n",
           DEVICE_NAME);
    return 0;

cleanup:
    device_destroy(monitor_class, MKDEV(major_number, 0));
    class_destroy(monitor_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    return ret;
}

/* ─── Module exit ───────────────────────────────────────────────────── */
static void __exit monitor_exit(void)
{
    unregister_kprobe(&kp_write);
    unregister_kprobe(&kp_read);
    unregister_kprobe(&kp_open);
    device_destroy(monitor_class, MKDEV(major_number, 0));
    class_destroy(monitor_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "[monitor] Module unloaded.\n");
}

module_init(monitor_init);
module_exit(monitor_exit);
