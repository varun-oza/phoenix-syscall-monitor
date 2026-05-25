#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#define TESTFILE "/tmp/phoenix_perf.txt"
#define RUNS 10

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int main(void) {
    int fd;
    char buf[64];
    long long start, end, total = 0;

    printf("[sample] PID = %d\n", getpid());
    printf("[sample] Running %d iterations of open+read+write\n", RUNS);

    /* Create test file */
    fd = open(TESTFILE, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    write(fd, "hello phoenix\n", 14);
    close(fd);

    for (int i = 0; i < RUNS; i++) {
        start = now_ns();

        fd = open(TESTFILE, O_RDWR);
        read(fd, buf, sizeof(buf)-1);
        write(fd, "x", 1);
        close(fd);

        end = now_ns();
        total += (end - start);
        printf("[sample] Run %2d: %lld ns\n", i+1, end-start);
        sleep(1);
    }

    printf("[sample] Average: %lld ns (%.3f ms)\n",
           total/RUNS, (double)(total/RUNS)/1e6);
    unlink(TESTFILE);
    return 0;
}
