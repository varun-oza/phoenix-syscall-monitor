# Phoenix Syscall Monitor

A Linux kernel module implementing system call interception, logging, and blocking - developed as a technical test for the Mitacs Summer Research Internship at the Security Research Lab, Concordia University.

The implementation is inspired by the [Phoenix paper (NDSS 2024)](https://dx.doi.org/10.14722/ndss.2024.24582) by Kermabon-Bobinnec et al., which proposes a system for protecting containers against unpatched vulnerabilities by filtering sequences of system calls.

---

## Overview

This project consists of three components:

| Component | Description |
|---|---|
| `kernel_module/` | Linux kernel module using kprobes to intercept `open`, `read`, and `write` syscalls with OFF / LOG / BLOCK modes, controlled via ioctl |
| `userspace/` | C control utility that communicates with the kernel module via ioctl, implements CLI flags, JSON parsing, and a finite state machine |
| `test_programs/` | Performance measurement programs used in Part 3 of the technical test |

---

## Requirements

- **OS:** Ubuntu 22.04 LTS
- **Kernel:** 6.8.x (tested on 6.8.0-117-generic)
- **Compiler:** gcc-12
- **Tools:** make, git

Install all dependencies:

```bash
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r) gcc-12 make git
```

---

## Project Structure

```
phoenix_test/
├── kernel_module/
│   ├── syscall_monitor.c     # Kernel module - kprobes, ioctl, char device
│   ├── syscall_monitor.h     # Shared header - ioctl command definitions
│   └── Makefile
├── userspace/
│   ├── control.c             # Userspace control utility
│   ├── fsm.json              # Example FSM input file
│   └── Makefile
└── test_programs/
    ├── sample.c              # Performance benchmark (open + read + write)
    └── reaction_test.c       # Reaction time measurement
```

---

## Part 1 - Kernel Module

### How It Works

The kernel module uses **kprobes** to attach handlers to `__x64_sys_openat`, `__x64_sys_read`, and `__x64_sys_write`. When any process calls these syscalls, the handler runs first and decides whether to log, block, or allow the call based on the current mode.

Three modes are supported:

| Mode | Behaviour |
|---|---|
| `OFF` | Module is disabled. All syscalls proceed normally. |
| `LOG` | Every `open`, `read`, or `write` call is logged to the kernel ring buffer with PID, process name, and arguments. |
| `BLOCK` | The specified syscall is blocked (returns `EPERM`) for the specified PID. |

Mode and target parameters are changed at runtime via **ioctl** through the character device `/dev/syscall_monitor`.

### Build

```bash
cd kernel_module
make
```

Expected output:
```
CC [M]  /root/phoenix_test/kernel_module/syscall_monitor.o
LD [M]  /root/phoenix_test/kernel_module/syscall_monitor.ko
```

### Load

```bash
sudo insmod syscall_monitor.ko
sudo chmod 666 /dev/syscall_monitor
sudo dmesg | tail -5
```

Expected output:
```
[monitor] Module loaded. /dev/syscall_monitor ready.
```

Verify device exists:
```bash
ls -la /dev/syscall_monitor
```

### Unload

```bash
sudo rmmod syscall_monitor
sudo dmesg | tail -3
# Expected: [monitor] Module unloaded.
```

### View Logs

```bash
sudo dmesg | grep monitor | tail -20
sudo dmesg -W | grep monitor     # live stream
```

---

## Part 2 - Userspace Control Utility

### Build

```bash
cd userspace
make
./control --help
```

### Usage

```bash
# Disable the module
./control --off

# Log all open() syscalls system-wide
./control --log --syscall open

# Log all read() syscalls system-wide
./control --log --syscall read

# Log all write() syscalls system-wide
./control --log --syscall write

# Block open() for a specific PID
./control --block --syscall open --pid 1234

# Block read() for a specific PID
./control --block --syscall read --pid 1234

# Run FSM from JSON file (LOG mode only)
./control --log --file fsm.json
```

### FSM JSON Format

The `--file` flag accepts a JSON file defining the syscall sequence as FSM states:

```json
{ "states": ["open", "read", "write"] }
```

The FSM watches for each syscall in sequence. Once observed, it transitions to the next state. After the last state, it loops back to the first.

**Example FSM output:**
```
[control] FSM loaded: open -> read -> write -> (loop)
[control] STATE 1/3: watching for 'open'
[control] Waiting for 'open' syscall...
[control] OBSERVED 'open'! (count 0 -> 9)
[control] TRANSITION: 'open' -> 'read'
[control] STATE 2/3: watching for 'read'
[control] Waiting for 'read' syscall...
[control] OBSERVED 'read'! (count 0 -> 242067)
[control] TRANSITION: 'read' -> 'write'
[control] STATE 3/3: watching for 'write'
[control] OBSERVED 'write'! (count 1 -> 2920)
[control] TRANSITION: 'write' -> 'open' (looping back to start)
```

> **Note:** Observation counts are elevated because kprobes intercept the target syscall system-wide - from all running processes, not only the test process.

---

## Part 3 - Performance Test Programs

### Build

```bash
cd test_programs
gcc -o sample sample.c
gcc -o reaction_test reaction_test.c
```

### Sample Program - Response Time Overhead

Runs 10 iterations of `open + read + write` on a test file with 1-second sleep between iterations. Measures per-iteration time using `clock_gettime(CLOCK_MONOTONIC)`.

```bash
# Baseline - module OFF
cd userspace && ./control --off
cd ../test_programs && ./sample

# With logging - module LOG
cd ../userspace && ./control --log
cd ../test_programs && ./sample
```

### Reaction Test - Userspace Detection Latency

Measures the time between a syscall being issued and the userspace program detecting it via the ioctl counter. Uses a busy-poll loop for minimum-latency measurement.

```bash
# Make sure module is loaded and LOG mode is active
cd userspace && ./control --log
cd test_programs && ./reaction_test
```

---

## Quick Start (Full Test)

```bash
# 1. Load the module
cd kernel_module && make
sudo insmod syscall_monitor.ko
sudo chmod 666 /dev/syscall_monitor

# 2. Build the utility
cd ../userspace && make

# 3. Test LOG mode
./control --log --syscall open
# In another terminal: cat /etc/hostname
# Check: sudo dmesg | grep monitor | tail -5

# 4. Test BLOCK mode
./control --block --syscall read --pid <PID>
# Check: sudo dmesg | grep BLOCKED

# 5. Test FSM
echo "hello phoenix" > /tmp/testfile.txt
./control --log --file fsm.json
# In another terminal: cat /tmp/testfile.txt

# 6. Turn off
./control --off

# 7. Unload
cd ../kernel_module && sudo rmmod syscall_monitor
```

---

## Known Limitations

- Kprobes fire system-wide - all processes are intercepted in LOG mode, not just the target process
- The FSM uses 100ms polling via ioctl, introducing up to 100ms reaction latency
- High system load (active GUI, background processes) increases measurement variance

---

## Reference

Kermabon-Bobinnec, H., Jarraya, Y., Wang, L., Majumdar, S., & Pourzandi, M. (2024).
*Phoenix: Surviving Unpatched Vulnerabilities via Accurate and Efficient Filtering of Syscall Sequences.*
Network and Distributed System Security Symposium (NDSS 2024).
https://dx.doi.org/10.14722/ndss.2024.24582
