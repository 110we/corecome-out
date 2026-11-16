# Third Kernel: OpenKernel

**Status:** initial foundation only  
**Product type:** independent, general-purpose, extensible operating-system kernel  
**Architecture target:** AArch64 (initial bring-up)

## 1. Product definition

OpenKernel is the third independent kernel. It is not RedLab, not a RedLab profile, and not a subsystem of COXVBN. It owns a separate source tree, boot path, linker layout, build target, runtime, platform interfaces, and roadmap.

The requirement “解除所有限制” is interpreted as removing inherited product-specific functional restrictions: OpenKernel does not inherit COXVBN capability masks, RedLab engagement-scope rules, or daily-profile restrictions. Its supported features and policy configuration are governed by OpenKernel itself and its operator.

This does not mean intentionally removing fundamental kernel integrity protections. Memory protection, privilege boundaries, resource ownership, and recovery mechanisms are correctness and safety foundations, not arbitrary feature restrictions.

## 2. Three-product boundary

| Product | Role | Build/runtime boundary |
|---|---|---|
| COXVBN | Existing foundational AArch64 kernel | Own boot image and kernel subsystems |
| RedLab | Authorized security-assessment and privacy-profile library | Separate host library today; not a bootable kernel |
| OpenKernel | Third independent general-purpose kernel | Own bootable kernel image and independent subsystem roadmap |

OpenKernel must not link RedLab policy code or inherit COXVBN's capability tables, domain masks, or profile transition logic. Shared code may be considered only through an explicit, reviewed, stable interface; independence is the default.

## 3. Initial source layout

```text
kernels/OpenKernel/
├── Makefile
├── README.md
├── arch/
│   └── aarch64/
│       ├── boot.S
│       └── linker.ld
├── core/
│   └── kernel.c
├── memory/          # planned
├── scheduler/       # planned
├── process/         # planned
├── ipc/             # planned
├── drivers/         # planned
├── network/         # planned
├── filesystem/      # planned
├── security/        # OpenKernel-owned design; not inherited policy
└── tests/           # planned
```

Implemented files are only the Makefile, AArch64 entry/linker, minimal kernel entry, and PL011 serial output. Directories marked planned are roadmap targets, not existing implementation.

## 4. Functional architecture

1. **Architecture and boot:** AArch64 entry, exception vectors, linker, platform discovery, SMP bring-up.
2. **Memory:** physical page allocator, virtual memory, page tables, kernel/user address spaces, mapping lifecycle.
3. **Execution:** task model, scheduler, context switching, timers, synchronization primitives.
4. **Process and ABI:** process lifecycle, system-call ABI, user-mode transition, loader and executable format.
5. **IPC:** message queues, shared-memory channels, event notification, resource handles.
6. **Direct Port Communication:** kernel-native paired endpoints with a direct bidirectional data path; no bridge process or relay port. Include ownership, endpoint pairing, flow control, backpressure, close/revoke, and race-safe buffer reclamation.
7. **Drivers and devices:** device model, driver lifecycle, interrupt routing, DMA ownership and I/O abstractions.
8. **Networking:** independent network stack and socket API; policy is OpenKernel-owned and configurable.
9. **Storage and filesystem:** block layer, VFS, filesystem implementations, mount and permission model.
10. **Extensibility:** stable module ABI, loader, versioning, dependency resolution, lifecycle and diagnostics.
11. **Security and recovery:** kernel integrity, isolation, audit hooks and recovery. No inherited RedLab target-scope or COXVBN product capability policy.

## 5. Functional openness

The base kernel will not hard-code the first two products' feature allowlists or their use-case-specific restrictions. Optional policy modules may be installed, configured, or removed by the authorized operator. The kernel still enforces memory safety, privilege transitions, device ownership, and resource lifetime so one extension cannot corrupt unrelated kernel state.

## 6. Build and verification gates

- [x] Separate source tree and standalone Makefile created.
- [x] Independent AArch64 entry, linker layout and serial-console foundation added.
- [ ] Cross-compile the OpenKernel image in CI.
- [ ] Boot under QEMU and assert serial output.
- [ ] Add host tests for boot metadata parsing and foundational allocators.
- [ ] Add exception/vector and interrupt-controller foundations.
- [ ] Implement memory management and scheduler before user mode.
- [ ] Implement syscall/IPC and direct port-to-port communication.
- [ ] Add driver, network, storage and filesystem subsystems.
- [ ] Add module ABI and independent extension lifecycle.
- [ ] Add integration, fault-injection, concurrency and regression tests.

A checkbox is complete only when code and automated evidence exist. The initial files have not yet been built or QEMU-tested in this change.
