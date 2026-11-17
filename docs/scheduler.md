# Task lifecycle and scheduler selection baseline

The scheduler is a bounded task lifecycle table with generation-bound references
and READY, RUNNING, BLOCKED, and ZOMBIE transitions. Task creation validates the
entry address, caller-owned stack alignment and size, and optional owner metadata.

Each task record now has a saved exception-frame pointer. A caller can attach a
16-byte-aligned first-run frame with `coxvbn_task_set_initial_frame()`. The
`coxvbn_scheduler_schedule_frame()` primitive saves the interrupted task's frame,
moves that task back to READY, and selects the next READY task with a frame. It
returns the selected frame for the exception-return path. Host tests exercise
first dispatch, round-robin frame selection, and preservation of each task's
saved ELR value.

The AArch64 first-run helper builds a frame in a caller-owned stack using the
register/ELR/SPSR layout consumed by exception-return assembly. It initializes
x0 with the task argument, x30 with a caller-supplied return trampoline, ELR with
the entry point, and SPSR for masked EL1h execution. The 304-byte allocation
preserves the assembly's 16-byte stack alignment while the C-visible frame
payload is 288 bytes.

This is an important scheduler primitive, but it is not yet a complete runtime
context switch: the exception dispatcher is not yet wired to a live scheduler,
no initial task is installed during boot, the return trampoline is not supplied,
and timer IRQ delivery remains disabled. The component still requires external
serialization and is not IRQ-safe or SMP-safe. Task stacks are caller-owned.

## State transitions

- create: UNUSED -> READY
- initial frame attach: READY -> READY (frame becomes eligible for dispatch)
- frame scheduling: RUNNING -> READY, then selected READY -> RUNNING
- yield: RUNNING -> READY
- block: RUNNING -> BLOCKED
- wake: BLOCKED -> READY
- exit: RUNNING -> ZOMBIE
- reap: ZOMBIE -> UNUSED

A task reference is valid only while its slot is occupied and its generation
matches. Reaping invalidates the reference. The optional owner remains metadata
until the scheduler is integrated with a trusted execution-context provider.

## Remaining execution work

- connect the frame scheduler to exception dispatch and install an initial task
- implement a safe task-return trampoline and task exit handoff
- add IRQ/preemption-safe serialization and per-CPU current-task state
- enable timer-driven preemption only after IRQ routing and EOI behavior are verified
- add idle-task integration, syscall ABI, and EL0/address-space isolation
- run runtime exception/IRQ tests and QEMU scheduling tests
