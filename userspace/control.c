#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <signal.h>

/* Same definitions as the kernel module */
#define MODE_OFF   0
#define MODE_LOG   1
#define MODE_BLOCK 2

#define SYSCALL_OPEN  0
#define SYSCALL_READ  1
#define SYSCALL_WRITE 2

#define MONITOR_MAGIC 0xBB
#define IOCTL_SET_MODE    _IOW(MONITOR_MAGIC, 0, int)
#define IOCTL_SET_PID     _IOW(MONITOR_MAGIC, 1, int)
#define IOCTL_SET_SYSCALL _IOW(MONITOR_MAGIC, 2, int)
#define IOCTL_GET_COUNT   _IOR(MONITOR_MAGIC, 3, int)

#define DEVICE_NAME "/dev/syscall_monitor"

/* ── FSM state machine ─────────────────────────────────────────────── */
#define MAX_STATES 32
#define MAX_NAME   16

typedef struct {
    char names[MAX_STATES][MAX_NAME];
    int  count;
} FSM;

/* ── Simple JSON parser for {"states": ["open","read","write"]} ────── */
static int parse_fsm_json(const char *filename, FSM *fsm)
{
    FILE *f;
    char  buf[4096];
    int   len;
    char *p, *start, *end;
    int   count = 0;

    fsm->count = 0;
    f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "[control] Cannot open file: %s\n", filename);
        return -1;
    }
    len = fread(buf, 1, sizeof(buf)-1, f);
    fclose(f);
    buf[len] = '\0';

    p = strstr(buf, "\"states\"");
    if (!p) { fprintf(stderr, "[control] Missing 'states' key\n"); return -1; }
    p = strchr(p, '[');
    if (!p) { fprintf(stderr, "[control] Missing '['\n"); return -1; }
    p++;

    while (*p && *p != ']' && count < MAX_STATES) {
        start = strchr(p, '"');
        if (!start || start >= strchr(p, ']')) break;
        start++;
        end = strchr(start, '"');
        if (!end) break;
        int nlen = (int)(end - start);
        if (nlen >= MAX_NAME) nlen = MAX_NAME - 1;
        strncpy(fsm->names[count], start, nlen);
        fsm->names[count][nlen] = '\0';
        count++;
        p = end + 1;
    }

    if (count == 0) {
        fprintf(stderr, "[control] No states found\n");
        return -1;
    }
    fsm->count = count;

    printf("[control] FSM loaded: ");
    for (int i = 0; i < count; i++)
        printf("%s%s", fsm->names[i], i < count-1 ? " -> " : " -> (loop)\n");

    return 0;
}

/* ── ioctl helpers ─────────────────────────────────────────────────── */
static int set_mode(int fd, int mode)
{
    if (ioctl(fd, IOCTL_SET_MODE, &mode) < 0) {
        perror("[control] SET_MODE failed");
        return -1;
    }
    printf("[control] Mode: %s\n",
           mode == MODE_OFF ? "OFF" : mode == MODE_LOG ? "LOG" : "BLOCK");
    return 0;
}

static int set_syscall(int fd, const char *name)
{
    int sc;
    if      (strcmp(name, "open")  == 0) sc = SYSCALL_OPEN;
    else if (strcmp(name, "read")  == 0) sc = SYSCALL_READ;
    else if (strcmp(name, "write") == 0) sc = SYSCALL_WRITE;
    else {
        fprintf(stderr, "[control] Unknown syscall: %s\n", name);
        return -1;
    }
    if (ioctl(fd, IOCTL_SET_SYSCALL, &sc) < 0) {
        perror("[control] SET_SYSCALL failed");
        return -1;
    }
    printf("[control] Watching syscall: %s\n", name);
    return 0;
}

static int set_pid(int fd, int pid)
{
    if (ioctl(fd, IOCTL_SET_PID, &pid) < 0) {
        perror("[control] SET_PID failed");
        return -1;
    }
    printf("[control] Target PID: %d\n", pid);
    return 0;
}

static int get_count(int fd)
{
    int count = 0;
    if (ioctl(fd, IOCTL_GET_COUNT, &count) < 0) {
        perror("[control] GET_COUNT failed");
        return -1;
    }
    return count;
}

/* ── FSM runner ────────────────────────────────────────────────────── */
static volatile int fsm_running = 1;

static void handle_sigint(int sig)
{
    (void)sig;
    printf("\n[control] Stopping FSM...\n");
    fsm_running = 0;
}

static void run_fsm(int fd, FSM *fsm)
{
    int state_idx  = 0;
    int last_count = 0;
    int curr_count = 0;

    signal(SIGINT, handle_sigint);

    printf("\n[control] FSM started. Press Ctrl+C to stop.\n");
    printf("[control] Tip: In another terminal, run: cat /tmp/test.txt\n\n");

    while (fsm_running) {
        const char *cur = fsm->names[state_idx];

        printf("[control] STATE %d/%d: watching for '%s'\n",
               state_idx + 1, fsm->count, cur);

        /* Tell kernel module: LOG mode, watch this syscall */
        set_mode(fd, MODE_LOG);
        set_syscall(fd, cur);

        /* Get current counter as baseline */
        last_count = get_count(fd);
        if (last_count < 0) { sleep(1); continue; }

        printf("[control] Waiting for '%s' syscall...\n", cur);

        /* Poll until counter increases */
        while (fsm_running) {
            usleep(100000); /* check every 100ms */
            curr_count = get_count(fd);
            if (curr_count < 0) continue;
            if (curr_count > last_count) {
                printf("[control] OBSERVED '%s'! (count %d -> %d)\n",
                       cur, last_count, curr_count);
                break;
            }
        }

        if (!fsm_running) break;

        /* Transition to next state */
        int next = (state_idx + 1) % fsm->count;
        printf("[control] TRANSITION: '%s' -> '%s'%s\n\n",
               fsm->names[state_idx],
               fsm->names[next],
               next == 0 ? " (looping back to start)" : "");

        state_idx = next;
        sleep(1);
    }

    set_mode(fd, MODE_OFF);
    printf("[control] FSM stopped.\n");
}

/* ── Usage ─────────────────────────────────────────────────────────── */
static void print_usage(const char *prog)
{
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("  --off                  Disable module\n");
    printf("  --log                  Enable logging\n");
    printf("  --block                Enable blocking\n");
    printf("  --syscall <name>       Set syscall: open, read, write\n");
    printf("  --pid <number>         Set target PID (for block mode)\n");
    printf("  --file <fsm.json>      Run FSM from JSON file\n\n");
    printf("Examples:\n");
    printf("  %s --log\n", prog);
    printf("  %s --log --syscall read\n", prog);
    printf("  %s --block --syscall open --pid 1234\n", prog);
    printf("  %s --log --file fsm.json\n", prog);
}

/* ── main ──────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    int   fd;
    int   mode        = -1;
    char *syscall_arg = NULL;
    int   pid_arg     = -1;
    char *fsm_file    = NULL;

    if (argc < 2) { print_usage(argv[0]); return 1; }

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--off")   == 0) mode = MODE_OFF;
        else if (strcmp(argv[i], "--log")   == 0) mode = MODE_LOG;
        else if (strcmp(argv[i], "--block") == 0) mode = MODE_BLOCK;
        else if (strcmp(argv[i], "--syscall") == 0) {
            if (++i >= argc) { fprintf(stderr, "Missing syscall name\n"); return 1; }
            syscall_arg = argv[i];
        }
        else if (strcmp(argv[i], "--pid") == 0) {
            if (++i >= argc) { fprintf(stderr, "Missing PID\n"); return 1; }
            pid_arg = atoi(argv[i]);
        }
        else if (strcmp(argv[i], "--file") == 0) {
            if (++i >= argc) { fprintf(stderr, "Missing filename\n"); return 1; }
            fsm_file = argv[i];
        }
        else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]); return 0;
        }
        else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    /* Validate */
    if (mode == -1) {
        fprintf(stderr, "[control] Error: specify --off, --log, or --block\n");
        return 1;
    }
    if (fsm_file && mode != MODE_LOG) {
        fprintf(stderr, "[control] Error: --file only works with --log\n");
        return 1;
    }

    /* Open device */
    fd = open(DEVICE_NAME, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[control] Cannot open %s: %s\n",
                DEVICE_NAME, strerror(errno));
        fprintf(stderr, "[control] Is the module loaded?\n");
        fprintf(stderr, "[control] Run: sudo insmod kernel_module/syscall_monitor.ko\n");
        return 1;
    }
    printf("[control] Connected to %s\n", DEVICE_NAME);

    /* Apply syscall and pid settings */
    if (syscall_arg && set_syscall(fd, syscall_arg) < 0) {
        close(fd); return 1;
    }
    if (pid_arg > 0 && set_pid(fd, pid_arg) < 0) {
        close(fd); return 1;
    }

    /* FSM mode */
    if (fsm_file) {
        FSM fsm;
        if (parse_fsm_json(fsm_file, &fsm) < 0) {
            close(fd); return 1;
        }
        run_fsm(fd, &fsm);
        close(fd);
        return 0;
    }

    /* Simple mode */
    if (set_mode(fd, mode) < 0) {
        close(fd); return 1;
    }

    if (mode == MODE_LOG) {
        printf("[control] Logging active.\n");
        printf("[control] Watch with: sudo dmesg -W | grep monitor\n");
        printf("[control] Stop with:  ./control --off\n");
    }

    close(fd);
    return 0;
}
