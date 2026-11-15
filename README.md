# CampusCompute — Level 3 Mini-Project

A Linux/Ubuntu C command-line prototype for CPU scheduling, synchronized logging,
POSIX shared-memory IPC, Banker's safety analysis, child-process monitoring, and
a process resource-limit demonstration.

## Prerequisites
- Ubuntu Linux or WSL2 with Ubuntu
- GCC and GNU Make
- POSIX threads, shared memory, and named semaphores (provided by normal Ubuntu libc)

Install build tools if needed:

```bash
sudo apt update
sudo apt install build-essential
```

## Build

```bash
make
```

## Commands

```bash
./campuscompute help
./campuscompute schedule fcfs
./campuscompute schedule rr 2
./campuscompute schedule priority
./campuscompute schedule fcfs --compare
./campuscompute logger 8
./campuscompute ipc
./campuscompute banker safe
./campuscompute banker unsafe
./campuscompute worker normal
./campuscompute worker fail
./campuscompute limit
./campuscompute demo
./campuscompute clean
```

The scheduling commands use the same four repeatable sample jobs. Lower numeric
priority means higher priority. FCFS ties are broken by earlier arrival and then
lower job ID. Priority scheduling is non-preemptive and uses the same arrival/job
ID tie-break after priority. Round Robin defaults to quantum 2.

The logger uses a bounded queue, a mutex, and condition variables. Events are
appended to `campuscompute.log`. The IPC command creates a uniquely named shared
memory object and named semaphore, forks a cooperating child, exchanges an
integer, and unlinks only those objects after use. Banker's examples use a
standard five-process, three-resource test state; the unsafe case intentionally
sets Available to zero. The `worker fail` case is an explicitly simulated
non-zero exit, not a real operating-system failure. The resource-limit command
sets `RLIMIT_FSIZE` in a child and demonstrates the effect on a temporary file.

## Cleanup

```bash
./campuscompute clean
make clean
```

`clean` removes only this project's log/temp file and its own named IPC objects.
Do not use broad system-wide IPC cleanup commands.

