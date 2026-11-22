# Task lifecycle and scheduler selection baseline

The scheduler is a bounded task lifecycle table with generation-bound references
and READY, RUNNING, BLOCKED, and ZOMBIE transitions. Task creation validates the
entry address, caller-owned stack alignment and size, and optional owner metadata.

Each task record now has a saved exception-frame pointer. A caller can attach a
16-byte-aligned first-run frame with `coxvbn_task_set_initial_frame()`; the
frame's full 304-byte assembly allocation must lie inside that task's owned stack.
The same range check is applied when saving an interrupted frame, preventing a
pointer into a shared boot/exception stack or another task's stack from being retained.
The `coxvbn_scheduler_schedule_frame()` primitive saves the interrupted task's frame,
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

This now includes the first real EL1 task handoff. `kernel_main()` builds the
initial frame in a dedicated, statically owned task stack, registers it with the
scheduler, and enters it through `coxvbn_aarch64_enter_frame()`, which shares the
exception-return register restore path. The initial task runs with masked IRQs;
its first exception frame will therefore be allocated on its own SP_EL1 stack.
The exception dispatcher can be attached to a scheduler and, after a real GIC
IRQ has been acknowledged and serviced, save the interrupted task frame and
return the next READY task frame. The host exception-dispatch test exercises
round-robin handoff between two task-owned frames.

Two EL1 tasks now use a narrow internal SVC task-control ABI:
`SVC #0` yields, and `SVC #1` retires the current task through its return
trampoline. The dispatcher accepts only the AArch64 SVC exception class from
the current-EL SPx vector. For `SVC #0`, it preserves ELR_EL1 as captured:
AArch64 records the address of the instruction after SVC, so adding four bytes
again would skip a caller instruction and corrupt control flow. Host tests
assert ELR preservation and task-exit state.

The boot path creates two task-owned stacks and enters the first task. Task A
first performs a bounded busy wait without yielding. The ARMv8 physical timer
is programmed as a one-shot source and GICv2 PPI 30 is enabled only after the
handler and both task frames are ready. On each timer IRQ, the handler rearms
the one-shot timer; the dispatcher acknowledges the interrupt, invokes the
registered handler, writes the original token to EOI, and can return the next
ready task frame through the common `eret` path. QEMU serial CI asserts the
first timer tick, repeated timer delivery, task B running before task A's busy
wait completes, cooperative rotation, and task A's exit handoff. This validates
a single-core QEMU timer-preemption path; it does not establish SMP or
preemption-safe locking.

A task that returns enters the `SVC #1` exit ABI through its return trampoline.
The dispatcher retires the running task, records the signed 32-bit result, drops
its saved frame, and selects another READY task. If no saved task remains, the
kernel fails closed rather than returning through a dead stack. IRQ-safe
serialization, per-CPU state, and timer preemption remain outstanding. Task
stacks are caller-owned.

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

- add IRQ/preemption-safe synchronization and per-CPU current-task state before SMP
- generalize timer sources and add an idle task with explicit wakeup semantics
- add EL0/address-space isolation and a userspace syscall ABI
- validate the same IRQ/preemption path on the user's Vectras VM and additional hardware
