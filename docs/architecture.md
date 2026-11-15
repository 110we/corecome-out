# Architecture baseline

COXVBN is a new operating-system implementation, not a Linux distribution. The initial engineering target is AArch64 on the QEMU `virt` machine so that each low-level milestone can be reproduced in CI.

## Initial boot contract

The first kernel image assumes the QEMU AArch64 direct-kernel boot environment and starts at the image entry point. The assembly entry masks interrupts, establishes a private bootstrap stack, clears BSS, and calls `kernel_main`. The C kernel writes a boot marker to the QEMU PL011 UART.

This is deliberately a narrow contract. It does not yet claim to support arbitrary firmware handoff, EL3 transitions, secondary CPUs, MMU activation, interrupt controllers, scheduling, or userspace.

## Design direction

- A modular monolithic kernel with explicit subsystem interfaces.
- AArch64 first; additional architectures only after the core contracts stabilize.
- Kernel-owned enforcement for memory isolation and privileged operations.
- One central Security Plugin API for policy decisions requested by tools and services; it cannot override hardware or kernel-enforced safety boundaries.
- Compatibility layers are later projects, not substitutes for native kernel foundations.
- The Key Vault, Information Store, and Emergency Recovery are separate modules with independent APIs. See [recovery-and-stores.md](recovery-and-stores.md).

## Implementation discipline

Every milestone must identify its supported machine configuration, initialization order, invariants, and automated test. Documentation must distinguish implemented behavior from planned behavior.
