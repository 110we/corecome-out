# CORECOME Kernel Architecture

## Product direction

CORECOME is a standalone, general-purpose operating system with a **modular monolithic kernel**. It is not a microkernel, a Linux distribution, or merely a collection of security tools. The kernel is independently designed and bootable; Linux compatibility is provided by implementing compatible interfaces and runtime services rather than depending on the Linux kernel.

The architecture deliberately keeps performance-critical operating-system facilities in privileged kernel space while maintaining strict internal subsystem boundaries.

## Non-negotiable design

1. **Modular monolithic kernel.** Scheduler, virtual memory, syscall dispatch, VFS core, networking core, IPC primitives, security reference monitor, and driver framework run in kernel space. Drivers and kernel facilities may be built in or loaded as kernel modules where safe and practical.
2. **Standalone boot.** CORECOME boots and operates without a Linux kernel or Linux user space. AArch64 is the first-class architecture and QEMU virt is the first validation platform.
3. **Linux compatibility as a first-class project.** Implement a documented Linux-compatible syscall/ABI layer, ELF loading, process and thread semantics, signals, file descriptors, virtual filesystem behavior, sockets, and common device interfaces in stages. Compatibility must be measured with test suites, not claimed from superficial API resemblance.
4. **Native system identity.** CORECOME has its own init/system manager, shell, service model, package format, security policy, and eventually a native desktop. Linux applications may run through compatibility interfaces; native operation must not depend on them.
5. **Capability-based authority.** Kernel objects and privileged operations are protected by explicit rights and policy checks. Compatibility interfaces must not silently bypass CORECOME security policy.
6. **Authorized security testing.** Active assessment jobs require explicit, bounded authorization scopes with target identities, permitted operations, rate/resource limits, expiry, and revocation. Out-of-scope actions are denied by default.
7. **Audit and emergency stop.** Authority grants, scope decisions, launches, and stop events are auditable. The stop path must revoke assessment capabilities and terminate associated jobs.

## Kernel subsystem map

- Architecture/boot: EL transitions, exception vectors, interrupt controller, timer, SMP startup, context switching
- Memory management: physical page allocator, virtual memory, address spaces, memory mapping, copy-on-write, kernel heap
- Process core: tasks, threads, scheduler, signals, process lifecycle, credentials
- Syscall and ABI: native CORECOME ABI plus Linux-compatible syscall personality
- VFS: vnode/inode layer, mount namespace, file descriptors, permissions, filesystem drivers
- Networking: socket API, Ethernet, ARP/ND, IPv4/IPv6, UDP, TCP, routing, packet filtering
- Drivers: bus/device model, driver registration, DMA, interrupt handling, virtio and platform drivers
- Security: capability/object-rights model, policy engine, scope validator, audit ring, emergency-stop controller
- Compatibility: ELF loader, Linux syscall translation, Linux process semantics, ioctl and device compatibility, runtime/library support
- User space: native init, service manager, shell, package manager, authorization console, assessment workers
- Desktop: graphics, input, compositor, window manager, accessibility, and native applications

## Compatibility strategy

Compatibility is layered and incremental:

1. **Executable foundation:** ELF64/AArch64 loading, process image setup, dynamic linking support, TLS, and a stable syscall ABI.
2. **POSIX foundation:** file descriptors, pipes, process creation/exec/wait, signals, threads, clocks, memory mapping, and permissions.
3. **Linux ABI:** Linux AArch64 syscall numbers and semantics, futexes, epoll, eventfd, timerfd, ioctl conventions, procfs/sysfs behavior, namespaces and cgroups where feasible.
4. **Linux application environment:** compatible runtime libraries, loader behavior, package/application environment, and common service expectations.
5. **Driver compatibility:** prioritize virtio and standard bus/device abstractions. Linux kernel module compatibility is a separate, very large undertaking and is not promised by syscall compatibility alone.

A compatibility claim must name the supported architecture, ABI surface, test suite, and known gaps. Passing a few simple programs is not equivalent to general Linux application compatibility.

## Kernel/user-space boundary

The kernel is monolithic for performance and compatibility, but subsystem interfaces remain explicit. User-space services are used for policy consoles, package management, desktop components, and isolated assessment workers. The kernel remains the final enforcement point for memory protection, capabilities, authorization scopes, resource limits, and emergency stop.

## Milestones

- M0: AArch64 boot and serial output
- M1: exception vectors, timer, GIC, panic path, early console
- M2: physical memory allocator, kernel heap, MMU, address spaces
- M3: scheduler, process/thread lifecycle, EL0 entry, native syscall ABI
- M4: ELF64 loader, file descriptors, process operations, signals, POSIX baseline
- M5: VFS, filesystem drivers, native init and shell
- M6: Linux AArch64 syscall personality and compatibility test harness
- M7: socket API and network stack; virtio network/block drivers
- M8: capability/policy/audit/stop enforcement integrated with all privileged paths
- M9: native package/service system and assessment workers
- M10: native desktop and broader hardware support

Each milestone requires reproducible builds, unit/integration tests, and QEMU boot tests. Compatibility is reported as a tested matrix, not as a blanket claim.
