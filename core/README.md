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
- One trusted Security Plugin for all tool permission requests and user consent; kernel-enforced grant validation, isolation, quotas, revocation and emergency stop

## Compatibility promise

“System-replacement-level” is the long-term goal, not current functionality. Compatibility is measured per CPU architecture, ABI, OS/API version, runtime, device class and tested application. PE parsing alone is not Windows compatibility; APK installation alone is not Android compatibility; ELF loading alone is not Linux compatibility. See [the compatibility plan](docs/compatibility.md).

Linux and POSIX are the first user-space compatibility target. Windows PE/Win32, Android ART/Bionic/Binder, selected BSD personalities and ChromeOS-related workflows follow as distinct projects. Driver compatibility is separate from application compatibility; foreign kernel drivers are not assumed to work.

## Current implementation status

The current tree is still an early M0/M1 bring-up, not a usable operating system. It includes a single-core kernel security reference monitor and a built-in Security Plugin broker path: permission requests are queued, an approval/denial decision is processed centrally, and grants are bound to requester, target, operation set, expiry and action quota. Kernel boot self-tests cover pending denial, approval, cross-tool and cross-target denial, quota exhaustion, revocation, explicit denial, expiry and emergency stop. This is not yet a production-ready security plugin: the approval decision is simulated by the boot test, and there is no real consent UI, authenticated process identity, cryptographically signed grant, trusted package verification, dynamic module loading, SMP-safe synchronization or real network/device mediation. MMU, allocator, exceptions, scheduler, EL0, syscalls, ELF loading, VFS, networking, drivers, desktop and compatibility runtimes remain future work.

## Build

Requires an AArch64 bare-metal GCC toolchain and QEMU system emulator:

    make -C core
    make -C core run

The build defaults to aarch64-none-elf-gcc. Override CROSS_COMPILE for a different toolchain prefix.

## Security rule

All tool permission requests and user consent flow through the trusted Security Plugin. No tool may authorize itself or present a competing system-level permission flow. The kernel must independently enforce each grant's requester identity, target, permitted operations, expiry, quotas and revocation. Missing or unavailable Security Plugin means deny by default. No compatibility personality or driver may bypass these controls. See [the Security Plugin design](docs/security-plugin.md).
