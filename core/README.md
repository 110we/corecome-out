# CORECOME Standalone Operating System

CORECOME is an independent general-purpose operating system with a **modular monolithic kernel**. Its long-term product goal is system-replacement-level compatibility with mainstream non-Apple operating systems: Linux, Windows, Android, BSD and Linux-based environments such as ChromeOS.

CORECOME is not based on the Linux kernel and is not a Linux distribution. It owns its kernel, driver model, native user space, desktop, security model and package/service system.

## Product goals

- Standalone boot and operation without another OS kernel
- Linux-class kernel scope: scheduler, virtual memory, syscall dispatch, VFS, networking, IPC, security enforcement and driver framework
- Native CORECOME init, services, shell, package system and desktop
- Linux/POSIX, Windows, Android and selected BSD application compatibility through separate ABI/runtime personalities
- AArch64 first for bring-up; x86_64 is a major follow-on architecture
- Cross-ISA execution only through a separately implemented and tested emulator/dynamic translator
- Own driver model, initially targeting QEMU virt and VirtIO, then broad PC and mobile hardware
- Kernel-enforced capabilities, authorization scopes, audit, quotas and emergency stop for security assessment workloads

## Compatibility promise

“System-replacement-level” is the long-term goal, not current functionality. Compatibility is measured per CPU architecture, ABI, OS/API version, runtime, device class and tested application. PE parsing alone is not Windows compatibility; APK installation alone is not Android compatibility; ELF loading alone is not Linux compatibility. See [the compatibility plan](docs/compatibility.md).

Linux and POSIX are the first user-space compatibility target. Windows PE/Win32, Android ART/Bionic/Binder, selected BSD personalities and ChromeOS-related workflows follow as distinct projects. Driver compatibility is separate from application compatibility; foreign kernel drivers are not assumed to work.

## Current implementation status

The current M0 tree contains only an early AArch64 boot path and UART diagnostics. It is not yet a usable operating system and has no implemented Linux, Windows, Android or BSD compatibility runtime. MMU, allocator, exceptions, scheduler, EL0, syscalls, ELF loading, VFS, networking, drivers, desktop and compatibility layers remain future work.

## Build

Requires an AArch64 bare-metal GCC toolchain and QEMU system emulator:

    make -C core
    make -C core run

The build defaults to aarch64-none-elf-gcc. Override CROSS_COMPILE for a different toolchain prefix.

## Security rule

No active security assessment operation may run without an explicit bounded authorization scope. The kernel must enforce target identity, permitted operations, expiry, rate/resource limits, revocation, audit and emergency stop. No compatibility personality may bypass these controls.
