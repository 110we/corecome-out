# Task lifecycle and scheduler selection baseline

The current scheduler component is a bounded, host-tested task lifecycle table and
round-robin READY-task selector. It provides generation-bound task references and
the READY, RUNNING, BLOCKED, and ZOMBIE transitions, including wake, exit, and
reap behavior. Task creation validates the entry address, stack alignment, stack
size, and optional owning-module identity.

This is not yet a runnable scheduler. The selector changes bookkeeping state only and does not perform a context switch,
program a preemption timer, or enter user mode. A separate AArch64 helper now builds
a validated first-run frame in a caller-owned stack using the same register/ELR/SPSR
layout consumed by the exception-return assembly. It initializes x0 with the task
argument, x30 with a caller-supplied return trampoline, ELR with the entry point, and
SPSR for masked EL1h execution. The 304-byte allocation preserves the assembly's
16-byte stack alignment while the C-visible exception-frame payload is 288 bytes.
The helper does not install the frame, switch SP, or supply the return trampoline. All callers must serialize access; the component is not IRQ-safe or
SMP-safe. The stack memory is supplied by the caller and is not allocated or
freed by this component.

## State transitions

- create: UNUSED -> READY
- pick_next: READY -> RUNNING
- yield: RUNNING -> READY
- block: RUNNING -> BLOCKED
- wake: BLOCKED -> READY
- exit: RUNNING -> ZOMBIE
- reap: ZOMBIE -> UNUSED

A task reference is valid only while its slot is occupied and its generation
matches. Reaping invalidates the reference. No user-provided task reference is
accepted as proof of module identity; the optional owner is metadata only until
the scheduler is integrated with the trusted execution-context provider.

## Remaining execution work

- AArch64 context-save/restore integration, installing first-run frames, and a safe task-return trampoline
- per-CPU current-task state and IRQ/preemption-safe synchronization
- timer-driven preemption and idle-task integration
- module identity binding to trusted current-task state
- process address-space ownership, syscall ABI, and EL0 transition
- runtime exception/IRQ tests and QEMU scheduling tests
