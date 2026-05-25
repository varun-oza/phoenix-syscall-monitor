#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>

#define MONITOR_MAGIC   0xBB
#define IOCTL_SET_MODE    _IOW(MONITOR_MAGIC, 0, int)
#define IOCTL_SET_SYSCALL _IOW(MONITOR_MAGIC, 2, int)
#define IOCTL_GET_COUNT   _IOR(MONITOR_MAGIC, 3, int)
#define MODE_LOG    1
#define SYSCALL_OPEN 0

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int main(void) {
    int fd = open("/dev/syscall_monitor", 2);
    if (fd < 0) { perror("open device"); return 1; }

    /* Set LOG mode watching open */
    int mode = MODE_LOG, sc = SYSCALL_OPEN;
    ioctl(fd, IOCTL_SET_MODE, &mode);
    ioctl(fd, IOCTL_SET_SYSCALL, &sc);

    /* Get baseline count */
    int before = 0;
    ioctl(fd, IOCTL_GET_COUNT, &before);

    /* Trigger ONE open syscall and measure reaction time */
    long long t_syscall = now_ns();
    int tfd = open("/tmp/reaction_test.txt",
                   O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (tfd >= 0) close(tfd);

    /* Poll until count increases */
    int after = before;
    long long t_observed = 0;
    while (after <= before) {
        ioctl(fd, IOCTL_GET_COUNT, &after);
        if (after > before) {
            t_observed = now_ns();
            break;
        }
        /* busy poll — no sleep, measuring raw reaction */
    }

    long long reaction = t_observed - t_syscall;
    printf("[reaction] Syscall fired at:    t=0\n");
    printf("[reaction] Userspace noticed at: t=%lld ns\n", reaction);
    printf("[reaction] Reaction time: %lld ns (%.3f ms)\n",
           reaction, (double)reaction / 1e6);
    printf("[reaction] Polling interval in FSM: 100ms = 100,000,000 ns\n");
    printf("[reaction] Max possible FSM lag:    100ms\n");

    close(fd);
    unlink("/tmp/reaction_test.txt");
    return 0;
}

