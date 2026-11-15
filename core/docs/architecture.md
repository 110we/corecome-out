# CORECOME Kernel Architecture

## Non-negotiable design
1. Standalone bootable OS: no Linux kernel, Linux distribution, or host penetration-tool collection.
2. AArch64-first and QEMU-first. C11 for kernel subsystems, assembly only for architectural entry/exit and context-sensitive operations.
3. Small privileged kernel. Drivers, network protocols, filesystem servers, user shell, and test tools move to isolated user space as soon as process and IPC foundations exist.
4. Capability-based authority. A process receives only explicit handles and rights; ambient root-style authority is not the default.
5. Authorization scope is a kernel-enforced object, not a UI checkbox. Active testing requires a locally approved scope with target identities, allowed operations, rate/resource limits, expiry, and revocation.
6. Audit and stop are foundational services. Every authority grant, scope decision, test launch, and stop event is recorded. The stop path revokes test capabilities and terminates test jobs.

## Planned layers
- Boot/arch: EL transition, exception vectors, timer, interrupt controller, context switch
- Memory: physical page allocator, stage-1 translation, kernel/user address spaces, guarded mappings
- Core: scheduler, process lifecycle, IPC, syscall dispatch, timers
- Security: capability table, policy engine, scope validator, audit ring, emergency-stop controller
- Device: UART, virtio block/net, GIC, generic driver interface
- Services: VFS, network stack, name service, logging, package/boot manifest
- User space: native shell, system manager, authorization console, isolated assessment workers
- Desktop: later milestone, after stable process, graphics, input, and service ABI

## Milestones
- M0: AArch64 boot and serial output
- M1: exceptions, timer, GIC, panic path
- M2: physical memory allocator and MMU
- M3: EL0 transition, syscall ABI, process scheduler
- M4: IPC and capability/policy/audit/stop core
- M5: VFS and native shell
- M6: network stack and isolated authorized assessment workers
- M7: native desktop and signed system update path

Each milestone requires tests and a bootable QEMU image before the next subsystem is treated as complete.
