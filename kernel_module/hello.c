#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Varun Oza");
MODULE_DESCRIPTION("kprobe test - intercept open syscall");

/* This function runs EVERY TIME any process calls open() */
static int handler_pre_open(struct kprobe *p, struct pt_regs *regs)
{
    /* current = the process that just called open()
     * current->pid  = its process ID
     * current->comm = its name (e.g. "bash", "ls", "nginx") */
    printk(KERN_INFO "[kprobe] open() called by PID=%d COMM=%s\n",
           current->pid, current->comm);
    return 0; /* 0 = let the syscall run normally */
}

static struct kprobe kp_open = {
    .symbol_name = "__x64_sys_openat", /* kernel function to hook */
    .pre_handler = handler_pre_open,   /* our function to call   */
};

static int __init kprobe_init(void)
{
    int ret = register_kprobe(&kp_open);
    if (ret < 0) {
        printk(KERN_ERR "[kprobe] register_kprobe failed: %d\n", ret);
        return ret;
    }
    printk(KERN_INFO "[kprobe] Hooked onto open() successfully!\n");
    return 0;
}

static void __exit kprobe_exit(void)
{
    unregister_kprobe(&kp_open);
    printk(KERN_INFO "[kprobe] Unhooked from open()\n");
}

module_init(kprobe_init);
module_exit(kprobe_exit);
