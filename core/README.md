# CORECOME Standalone Security Kernel

This tree is a clean-room kernel implementation track, isolated from the legacy kernel and services while the new foundation is brought up.

## First target
- AArch64, QEMU virt, PL011 UART
- C11 plus AArch64 assembly; no Linux kernel, libc, or Linux user-space dependency
- EL1 kernel with an EL0 user-space boundary planned from the first design revision
- Capability-based authority, explicit target scopes, audit trail, and emergency stop
- Reproducible cross-build and QEMU boot test

## Current implementation
The initial milestone establishes a bootable EL1 foundation and UART diagnostics only. It is not yet a usable operating system and does not yet implement process isolation, virtual memory, a syscall ABI, networking, filesystems, or a penetration-testing toolchain.

## Build
Requires an AArch64 bare-metal GCC toolchain and QEMU system emulator:
    make -C core
    make -C core run

The build defaults to aarch64-none-elf-gcc. Override CROSS_COMPILE for a different toolchain prefix.

## Security rule
No active test operation may run without an explicit, bounded authorization scope. The future execution broker must reject out-of-scope targets by default, record decisions, enforce resource limits, and support a non-bypassable stop path.
