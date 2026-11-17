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

Two EL1 tasks now use the narrow `SVC #0` cooperative-yield ABI. The
exception dispatcher accepts only the AArch64 SVC exception class with immediate
zero from the current-EL SPx vector, advances ELR by four bytes, and schedules
the next saved task frame. Other synchronous exceptions and unsupported SVC
numbers still fail closed. Host tests verify the SVC handoff and ELR advancement.

The boot path creates two task-owned stacks and enters the first task. Both tasks
yield through SVC, allowing QEMU to exercise real exception entry, saved-frame
ownership, scheduler rotation, and exception return without enabling external
IRQ delivery. GIC delivery and the physical timer IRQ remain disabled, so this
is cooperative—not preemptive—scheduling.

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

- add IRQ/preemption-safe serialization and per-CPU current-task state
- enable timer-driven preemption only after IRQ routing and EOI behavior are verified
- add idle-task integration and EL0/address-space isolation
- run hardware IRQ, timer-preemption, and Vectras VM validation
