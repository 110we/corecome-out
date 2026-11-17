# Architecture baseline

COXVBN is a new operating-system implementation, not a Linux distribution. The current engineering target is AArch64 on the QEMU `virt` machine with GICv2 explicitly selected (`gic-version=2`) so that low-level milestones can be reproduced in CI.

## Implemented boot path

The current image is linked for the QEMU AArch64 direct-kernel boot path at `0x40080000`. Its entry assembly masks interrupts, preserves the incoming FDT pointer, installs the EL1 vector-table base, establishes a bootstrap stack, clears BSS, and calls `kernel_main`. The C kernel initializes a bounded in-memory VFS instance and performs a file write/read integrity self-test, then initializes the PL011 console, parses the FDT, initializes the bounded physical page allocator, and enables an initial EL1 identity map.

The initial translation regime is intentionally machine-specific: a 39-bit VA, 4-KiB granule, 2-MiB block mappings, QEMU virt RAM in the supported 0x40000000–0x80000000 window, and known GIC/PL011 device windows. It is not yet a general-purpose virtual-memory manager and does not provide per-process address spaces or EL0 isolation.

An aligned 16-entry EL1 vector table is installed. Each slot records its architectural vector index (0–15) before the common register saver runs. The exception frame preserves x0–x30 plus ELR_EL1, SPSR_EL1, ESR_EL1, FAR_EL1, and the vector index. Fatal diagnostics identify the vector class (current EL SP0, current EL SPx, lower AArch64, or lower AArch32; sync/IRQ/FIQ/SError) alongside the decoded ESR exception class. Vector-name decoding is shared, bounds-checked, and host-tested for all 16 slots plus invalid indices. Frame offsets and payload size are compile-time asserted and host-tested.

All vectors still take the fatal path: there is no IRQ dispatch, exception return path, or recoverable fault handling yet. IRQ/FIQ entries are identified for diagnostics only; interrupts remain masked.

## Current limitations

- The supported boot/test target is QEMU `virt` with GICv2 selected explicitly; CI compiles the generated device tree and checks the GICv2 compatibility and interrupt-controller property. Arbitrary firmware handoff, EL3 transitions, and secondary CPUs are not supported.
- Interrupts are masked at boot. A GICv2 quiescent-initialization routine accepts explicit distributor/CPU-interface bases, disables both interfaces, reads the implemented interrupt-line count, and prepares the priority mask. It deliberately does not enable either interface; timer interrupts, IRQ dispatch, acknowledgement/EOI, and exception return remain unimplemented.
- There is no scheduler, kernel-thread context switch, user mode, syscall ABI, or process address-space isolation. A bounded in-memory VFS instance is initialized and self-tested during boot; it remains non-persistent and has no block-device or crash-consistent storage backend. A bounded device lifecycle registry now provides probe/remove/suspend/resume hooks and generation-bound references, but it does not enumerate buses or bind hardware drivers.
- The physical allocator is a bounded bootstrap allocator; page-table allocation and dynamic virtual mappings are not implemented.
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
