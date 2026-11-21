# CORECOME System-Replacement Compatibility Plan

## Goal and honest definition

The long-term goal is to make CORECOME a practical replacement for mainstream non-Apple operating systems for common desktop, server, and mobile workflows. This is a compatibility objective, not a claim that every binary, driver, game, peripheral, or vendor service can run.

Compatibility is tracked separately by:
- CPU architecture and instruction-set level
- executable/container format
- system-call and user API behavior
- runtime and framework availability
- graphics, audio, input, and service integration
- kernel driver and device support
- application-specific acceptance tests

A compatibility badge is not awarded from file-format recognition alone. Each supported combination must have repeatable tests and a published known-limitations list.

## Compatibility families

### Linux and POSIX
- ELF32/ELF64 loaders, beginning with AArch64 ELF64
- POSIX process, file, signal, thread, memory, clock, IPC, and socket behavior
- Linux AArch64 syscall personality, including versioned syscall tables and semantic tests
- Dynamic loader and common C/C++ runtime expectations
- procfs/sysfs-like interfaces, futex, epoll, eventfd, timerfd, namespaces and cgroups in staged scope
- Linux application packaging and service conventions where practical

### Windows
- PE32/PE32+ parsing and loader, beginning with PE32+ x86_64
- Windows x64 calling convention, structured exception behavior, TLS, DLL loading and import resolution
- Win32 API families: Kernel, file, process/thread, synchronization, registry, user interface, graphics, audio and networking
- NT object and handle semantics required by applications
- Direct3D/DirectX translation is a separate graphics project
- Kernel-mode Windows drivers are not treated as user applications and are not promised by Win32 compatibility

### Android
- APK package inspection and install lifecycle
- ART managed runtime and Android framework APIs
- Bionic libc and Android NDK ABI support
- JNI and native ELF shared libraries
- Binder IPC, services, permissions, intents, storage and lifecycle semantics
- Android HAL/device integration is a distinct hardware enablement project; Google Play services and proprietary vendor components require separate licensing and compatibility work

### BSD
- POSIX baseline plus selected FreeBSD/OpenBSD/NetBSD ABI and system-call personalities
- ELF loading, BSD-specific sysctl, kqueue, socket and filesystem semantics as selected
- Compatibility is per-BSD family and release; “BSD compatible” must not be a blanket label

### ChromeOS and other Linux-based environments
- Linux application environment is the first compatibility path
- Container/runtime integration and Android app support are separate deliverables
- ChromeOS-specific services, verified boot assumptions and Google proprietary components are not implied by Linux ABI support

## CPU architecture plan

1. AArch64 is the native bring-up and first compatibility target.
2. x86_64 is the next major native architecture because of desktop application and hardware coverage.
3. ARM64 and x86_64 foreign binaries are supported only when their ABI and runtime are implemented.
4. Cross-ISA execution requires a separately tested emulator or dynamic binary translator; it is not supplied by an executable loader.
5. 32-bit compatibility is an explicit later milestone, not assumed by 64-bit support.

## Driver and hardware compatibility

CORECOME will own its kernel driver model. Initial device targets are QEMU virt devices and VirtIO. Next targets are standard PCI/PCIe enumeration, USB host controllers, NVMe, AHCI/SATA, common GPUs, audio, Wi-Fi and Ethernet.

A driver built for another kernel is not automatically portable: kernel object models, interrupt APIs, DMA rules, memory models and internal interfaces differ. Linux/Windows driver compatibility therefore requires dedicated compatibility subsystems and per-driver validation; it is not a consequence of application ABI compatibility.

## Architecture requirements

The modular monolithic kernel must provide stable internal interfaces for:
- task/process/thread and handle/object management
- virtual memory, mappings, shared memory and synchronization primitives
- VFS, file descriptors and mount namespaces
- sockets, packet I/O and network policy
- interrupt, DMA, bus and device frameworks
- security reference monitor, capability grants and audit

Compatibility personalities live behind explicit ABI dispatch and runtime boundaries. They must translate into common kernel primitives without bypassing CORECOME policy, quotas, audit, or emergency stop. Each personality has its own syscall/API tables, structure definitions, error mapping and conformance suite.

## Acceptance gates

For every compatibility family:
1. Publish supported CPU/ABI/version matrix.
2. Add loader/parser tests including malformed and adversarial inputs.
3. Add syscall/API conformance tests and regression tests.
4. Run representative real applications in automated integration tests.
5. Test graphics/audio/network/input and suspend/power behavior where relevant.
6. Record failures and unsupported APIs; never silently report success for unimplemented behavior.
7. Keep security boundaries enabled during compatibility tests.

## Delivery order

- C0: stable kernel foundation (boot, exceptions, SMP, MMU, allocator, scheduler)
- C1: native ABI, ELF64, dynamic linker contract, POSIX base
- C2: Linux AArch64 syscall personality and Linux userland test suite
- C3: VFS, network stack, graphics/input/audio foundations, native package/service system
- C4: Windows x86_64 PE loader and Win32 runtime foundation
- C5: x86_64 native platform and cross-ISA execution strategy
- C6: Android ART/Bionic/Binder/framework compatibility
- C7: selected BSD personalities and ChromeOS/Linux container workflows
- C8: hardware breadth, driver ports, power management, desktop and application certification

This is a multi-year operating-system program. The milestone order can evolve based on test results, but compatibility claims must remain evidence-based.
