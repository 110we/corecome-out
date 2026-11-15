# CORECOME Standalone Security Operating System

CORECOME is being built as an independent general-purpose operating system with a **modular monolithic kernel**, inspired by the broad subsystem scope and compatibility goals of Linux. It is not based on the Linux kernel and is not a Linux distribution.

## Architecture goals

- AArch64 first; QEMU virt first for repeatable bring-up
- Freestanding C11 and AArch64 assembly
- Kernel-space scheduler, virtual memory, syscall layer, VFS core, network stack, security enforcement, and driver framework
- Built-in and loadable kernel facilities with explicit internal interfaces
- Native init, service manager, shell, package system, and later desktop
- Linux-compatible ELF and syscall personality developed incrementally
- Kernel-enforced capabilities, authorization scopes, audit, quotas, and emergency stop for security assessment workloads

## Compatibility is a roadmap, not a current feature

The initial compatibility target is Linux AArch64 user-space ABI: ELF64 loading, process/thread behavior, file descriptors, signals, memory mapping, sockets, and a growing syscall set. POSIX behavior and common Linux interfaces will be added and validated in stages.

Linux kernel driver modules are not automatically compatible with a different kernel. Hardware support will initially use CORECOME's own driver model and prioritize virtio and standard device interfaces.

## Current implementation status

The current M0 tree contains only an early AArch64 boot path and UART diagnostics. It is not yet a usable operating system. MMU, allocator, exceptions, scheduler, EL0, syscalls, ELF loading, VFS, networking, drivers, compatibility runtime, shell, and desktop remain future work.

## Build

Requires an AArch64 bare-metal GCC toolchain and QEMU system emulator:

    make -C core
    make -C core run

The build defaults to aarch64-none-elf-gcc. Override CROSS_COMPILE for a different toolchain prefix.

## Security rule

No active security assessment operation may run without an explicit bounded authorization scope. The kernel must enforce target identity, permitted operations, expiry, rate/resource limits, revocation, audit, and emergency stop. Compatibility interfaces must not bypass these checks.
