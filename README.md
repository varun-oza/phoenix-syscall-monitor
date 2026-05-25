# Phoenix Syscall Monitor

Linux kernel module for syscall monitoring and filtering,
developed as a technical test for the Mitacs Summer Research
Internship at the Security Research Lab, Concordia University.

Inspired by the Phoenix paper (NDSS 2024) by Kermabon-Bobinnec et al.

## Project Structure
phoenix_test/
├── kernel_module/
│   ├── syscall_monitor.c   # Kernel module (kprobes + ioctl)
│   ├── syscall_monitor.h   # Shared header (ioctl definitions)
│   └── Makefile
├── userspace/
│   ├── control.c           # Userspace control utility
│   ├── fsm.json            # Example FSM definition
│   └── Makefile
└── test_programs/
├── sample.c            # Performance benchmark
└── reaction_test.c     # Reaction time measurement


## Requirements

- Ubuntu 22.04 LTS
- Kernel 6.8.x
- gcc-12
- linux-headers for your kernel version

## Build and Run

### Kernel Module
```bash
cd kernel_module
make
sudo insmod syscall_monitor.ko
sudo chmod 666 /dev/syscall_monitor
sudo dmesg | tail -5
```

### Userspace Utility
```bash
cd userspace
make
./control --help
```

## Usage

```bash
./control --off                              # disable module
./control --log --syscall open              # log open() calls
./control --log --syscall read              # log read() calls
./control --block --syscall open --pid 1234 # block open() for PID
./control --log --file fsm.json             # run FSM
```

## FSM JSON Format

```json
{ "states": ["open", "read", "write"] }
```

## Check Logs

```bash
sudo dmesg | grep monitor | tail -20
```

## Unload Module

```bash
sudo rmmod syscall_monitor
```

## Reference

Kermabon-Bobinnec, H., Jarraya, Y., Wang, L., Majumdar, S., & Pourzandi, M.
(2024). Phoenix: Surviving Unpatched Vulnerabilities via Accurate and
Efficient Filtering of Syscall Sequences. NDSS 2024.
https://dx.doi.org/10.14722/ndss.2024.24582
