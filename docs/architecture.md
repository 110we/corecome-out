# Architecture baseline

COXVBN is a new operating-system implementation, not a Linux distribution. The current engineering target is AArch64 on the QEMU `virt` machine with GICv2 explicitly selected (`gic-version=2`) so that low-level milestones can be reproduced in CI.

## Implemented boot path

The current image is linked for the QEMU AArch64 direct-kernel boot path at `0x40080000`. Its entry assembly masks interrupts, preserves the incoming FDT pointer, installs the EL1 vector-table base, establishes a bootstrap stack, clears BSS, and calls `kernel_main`. The C kernel initializes a bounded in-memory VFS instance and performs a file write/read integrity self-test, then initializes the PL011 console, parses the FDT, initializes the bounded physical page allocator, and enables an initial EL1 identity map.

The initial translation regime is intentionally machine-specific: a 39-bit VA, 4-KiB granule, 2-MiB block mappings, QEMU virt RAM in the supported 0x40000000–0x80000000 window, and known GIC/PL011 device windows. It is not yet a general-purpose virtual-memory manager and does not provide per-process address-space isolation or an EL0 transition. A separate host-tested page-table model supports bounded address-space descriptors and page mappings, but it is not yet wired into the live MMU or backed by the physical page allocator.

An aligned 16-entry EL1 vector table is installed. Each slot records its architectural vector index (0–15) before the common register saver runs. The exception frame preserves x0–x30 plus ELR_EL1, SPSR_EL1, ESR_EL1, FAR_EL1, and the vector index. Fatal diagnostics identify the vector class (current EL SP0, current EL SPx, lower AArch64, or lower AArch32; sync/IRQ/FIQ/SError) alongside the decoded ESR exception class. Vector-name decoding is shared, bounds-checked, and host-tested for all 16 slots plus invalid indices. Frame offsets and payload size are compile-time asserted and host-tested.

The exception entry/exit assembly saves and restores the complete general-purpose register frame and returns with `eret`. The EL1 dispatcher supports the internal `SVC #0` cooperative-yield and `SVC #1` task-exit operations; it preserves ELR_EL1 because AArch64 SVC entry already records the post-SVC instruction address. The C dispatcher also has a bounded, host-tested IRQ registration/dispatch path with GICv2 acknowledge/EOI handling and fail-closed unhandled-IRQ behavior. Hardware IRQ delivery remains disabled pending runtime IRQ validation; synchronous faults still take the fatal diagnostic path.

## Execution model status

A bounded task lifecycle table is present and host-tested. It provides generation-bound task references, READY/RUNNING/BLOCKED/ZOMBIE transitions, wake/exit/reap operations, and round-robin selection. The AArch64 context builder creates first-run frames in caller-owned stacks; the exception assembly saves/restores the general-purpose register frame and switches stacks through the common `eret` path. Two EL1 tasks exercise cooperative switching with internal `SVC #0`; `SVC #1` retires a returning task. The QEMU serial CI test verifies the task-switch and task-exit handoff. This is cooperative scheduling only: there is no timer-driven preemption, IRQ-safe serialization, per-CPU scheduler state, SMP support, or EL0 execution. See [scheduler.md](scheduler.md).

## Current limitations

- The supported boot/test target is QEMU `virt` with GICv2 selected explicitly; CI compiles the generated device tree and checks the GICv2 compatibility and interrupt-controller property. Arbitrary firmware handoff, EL3 transitions, and secondary CPUs are not supported.
- Interrupts are masked at boot. The GICv2 quiescent initialization and IRQ dispatcher are implemented and host-tested, but hardware IRQ delivery, timer PPI binding, and runtime exception/IRQ return tests remain open.
- The scheduler currently runs cooperative EL1 kernel tasks through internal SVC yield/exit operations. Timer-driven preemption, IRQ-safe synchronization, per-CPU state, user mode, a userspace syscall ABI, and process address-space isolation are not implemented. A bounded in-memory VFS instance is initialized and self-tested during boot; it remains non-persistent and has no block-device or crash-consistent storage backend. A bounded device lifecycle registry provides probe/remove/suspend/resume hooks and generation-bound references, but it does not enumerate buses or bind hardware drivers.
- The physical allocator is a bounded bootstrap allocator; dynamic page-table allocation and integration of the host-tested address-space model with the live MMU are not implemented.
- The Key Vault, Information Store, and Emergency Recovery are in-memory interfaces/contracts only. There is no persistent vault, audited cryptographic backend, watchdog takeover, network stack, or TCP listener. TCP port 3000 is not live.

## Design direction

- A modular monolithic kernel with explicit subsystem interfaces.
- AArch64 first; additional architectures only after the core contracts stabilize.
- Kernel-owned enforcement for memory isolation and privileged operations.
- One central Security Plugin API for policy decisions requested by tools and services; it cannot override hardware or kernel-enforced safety boundaries.
- Compatibility layers are later projects, not substitutes for native kernel foundations.
- The Key Vault, Information Store, and Emergency Recovery are separate modules with independent APIs. See [recovery-and-stores.md](recovery-and-stores.md).

## Implementation discipline

Every milestone must identify its supported machine configuration, initialization order, invariants, and automated test. Documentation must distinguish implemented behavior from planned behavior.
