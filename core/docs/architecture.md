# CORECOME Kernel Architecture

## Product direction

CORECOME is a standalone general-purpose operating system with a **modular monolithic kernel**. Its long-term goal is system-replacement-level compatibility with mainstream non-Apple operating systems, while retaining its own kernel, native user space, desktop, driver model and security architecture.

The compatibility target includes Linux/POSIX, Windows, Android, selected BSD families and Linux-based environments such as ChromeOS. This is a long-term engineering goal, not a claim of current support. Compatibility is tracked by architecture, ABI/API version, runtime, device class and repeatable application tests.

## Kernel design

1. Scheduler, virtual memory, syscall dispatch, VFS core, networking core, IPC primitives, security reference monitor and driver framework run in kernel space.
2. Subsystems have explicit interfaces, ownership rules, locking contracts and tests. Drivers and kernel facilities may be built in or loaded as modules where safe.
3. CORECOME boots and operates without a Linux kernel or Linux user space.
4. Native init, service manager, shell, package system and desktop are first-party components.
5. ABI personalities translate foreign OS behavior into shared kernel primitives; they cannot bypass CORECOME capabilities, policy, audit, quotas or emergency stop.
6. All user-facing permission decisions are centralized in the trusted Security Plugin. Tools request permissions from it and cannot authorize themselves. The kernel independently enforces the resulting bounded grants; missing or unavailable security authorization is denied by default.

## Kernel subsystem map

- Architecture/boot: EL transitions, exception vectors, interrupt controller, timer, SMP startup, context switching
- Memory: physical page allocator, virtual memory, address spaces, mappings, copy-on-write, kernel heap
- Process: tasks, threads, scheduler, signals, process lifecycle, credentials and handles
- ABI: native CORECOME ABI and versioned foreign ABI dispatch
- VFS: vnode/inode layer, mount namespaces, file descriptors, permissions, filesystem drivers
- Networking: socket API, Ethernet, ARP/ND, IPv4/IPv6, UDP/TCP, routing and packet policy
- Drivers: bus/device model, DMA, interrupt handling, VirtIO and platform drivers
- Security: one trusted Security Plugin owns permission policy, tool requests, user consent and grant lifecycle; the kernel reference monitor validates grants and enforces isolation, expiry, quotas, revocation and emergency stop on every privileged path. The first bring-up implementation is single-core and must be upgraded with IRQ-safe synchronization before SMP or real device/network operations. See [security-plugin.md](security-plugin.md).
- Compatibility runtimes: ELF and PE loaders, Linux/POSIX syscall personality, Win32 API runtime, Android ART/Bionic/Binder, selected BSD personalities
- User space: native init, service manager, shell, package manager, authorization console and isolated assessment workers
- Desktop: graphics, input, compositor, window manager, accessibility and native applications

## Compatibility families and sequencing

1. Linux/POSIX: ELF, process/thread semantics, signals, file descriptors, memory mapping, futex, epoll, sockets, dynamic loader and common userland.
2. Windows: PE32/PE32+ loading, x86_64 ABI, DLL loading, Win32 API families, NT handle/object semantics and graphics/audio integration.
3. Android: APK lifecycle, ART, Bionic, JNI, Android NDK ABI, Binder and framework services.
4. BSD: selected FreeBSD/OpenBSD/NetBSD ABI and system-call personalities, each tested separately.
5. ChromeOS-related workflows: Linux application environment first; ChromeOS-specific services and Android support remain separate deliverables.

AArch64 is the first native bring-up target. x86_64 is the next major native architecture for desktop breadth. Cross-ISA execution requires a separately tested emulator or dynamic binary translator. 32-bit execution is a separate compatibility milestone.

Driver and hardware support is independent of application compatibility. CORECOME first owns its driver model and targets QEMU virt/VirtIO, then standard PC and mobile buses/devices. Foreign Linux or Windows kernel drivers are not assumed to be compatible.

See [compatibility.md](compatibility.md) for the detailed scope, architecture requirements, acceptance gates and delivery order.

## Milestones

- M0: AArch64 boot and serial output
- M1: exception vectors, timer, GIC, panic path and early console
- M2: physical memory allocator, kernel heap, MMU and address spaces
- M3: scheduler, process/thread lifecycle, EL0 entry and native syscall ABI
- M4: ELF64 loader, file descriptors, process operations, signals and POSIX baseline
- M5: VFS, filesystem drivers, native init and shell
- M6: Linux AArch64 syscall personality and conformance harness
- M7: socket API, network stack and VirtIO block/network drivers
- M8: capability/policy/audit/stop enforcement across privileged paths
- M9: graphics/input/audio foundations, native package/service system
- M10: Windows PE/Win32 runtime foundation and x86_64 platform
- M11: Android ART/Bionic/Binder compatibility
- M12: selected BSD personalities and ChromeOS/Linux container workflows
- M13: broad hardware support, power management, native desktop and application certification

Every milestone requires reproducible builds, tests and QEMU or hardware validation. Compatibility is reported as a tested matrix with known gaps, never as a blanket claim.
