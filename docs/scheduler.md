# Task lifecycle and scheduler selection baseline

The current scheduler component is a bounded, host-tested task lifecycle table and
round-robin READY-task selector. It provides generation-bound task references and
the READY, RUNNING, BLOCKED, and ZOMBIE transitions, including wake, exit, and
reap behavior. Task creation validates the entry address, stack alignment, stack
size, and optional owning-module identity.

This is not yet a runnable scheduler. The selector changes bookkeeping state only:
it does not save or restore AArch64 registers, switch SP, construct an initial
exception frame, perform a context switch, program a preemption timer, or enter
user mode. All callers must serialize access; the component is not IRQ-safe or
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

- AArch64 context-save/restore assembly and first-run frame construction
- per-CPU current-task state and IRQ/preemption-safe synchronization
- timer-driven preemption and idle-task integration
- module identity binding to trusted current-task state
- process address-space ownership, syscall ABI, and EL0 transition
- runtime exception/IRQ tests and QEMU scheduling tests
