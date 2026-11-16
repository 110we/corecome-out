# COXVBN Project Family Progress Report

**Report date:** 2026-11-16  
**Repository:** `110we/corecome-out`  
**Active work:** COXVBN/RedLab separation PR #5 and stacked OpenKernel PR #6

## Product-line status

| Product | Actual role | Current implementation |
|---|---|---|
| COXVBN | Foundational AArch64 kernel | Boot foundation, FDT/memory/exception/interrupt foundations, host-tested Key Vault and module registry |
| RedLab | Authorized assessment and privacy-profile library; not an OS kernel | Separate host static library; scope/resource policy and profile transition contracts have host tests; no network I/O or independent boot |
| OpenKernel | Third independent, general-purpose AArch64 kernel | Independent boot entry, linker layout, PL011 serial output and standalone Makefile; AArch64 build and QEMU serial boot passed CI |

## OpenKernel boundary and status

OpenKernel is not RedLab and is not a COXVBN operating mode. It owns a separate source tree under `kernels/OpenKernel/`, its own AArch64 entry/linker, platform UART and build target. It does not compile or link COXVBN or RedLab source, and does not inherit their capability masks, target-scope restrictions, or daily-profile policy.

“Functionally unrestricted” means OpenKernel is not bound by the other products' use-case-specific feature allowlists. Memory safety, privilege separation, device ownership and resource lifetime remain kernel correctness requirements.

Implemented and CI-verified in this first slice:
- `kernels/OpenKernel/arch/aarch64/boot.S` and `linker.ld`
- `kernels/OpenKernel/core/kernel.c`
- `kernels/OpenKernel/platform/uart.c` and `uart.h`
- `kernels/OpenKernel/Makefile`
- Root `make openkernel` and `make run-openkernel` targets
- Independent architecture and completion plan at `docs/third-kernel/open-kernel.md`
- Independent CI build, ELF architecture check and QEMU serial boot assertion

CI run [43846739631](https://github.com/110we/corecome-out/actions/runs/43846739631) passed all three jobs: COXVBN AArch64/host tests, RedLab standalone build/tests, and OpenKernel independent build/QEMU boot. OpenKernel validation is limited to its initial boot/serial-console path.

Not yet implemented: MMU/page tables, physical allocator, exception vectors, interrupt delivery, SMP, scheduler, processes, user mode, syscalls, IPC, Direct Port Communication, drivers, networking, filesystem, module ABI and tests. Do not claim this is a complete or production-ready OS.

## COXVBN and RedLab status

- COXVBN builds its AArch64 image from `kernels/COXVBN/`; RedLab sources are excluded from that image.
- RedLab builds independently as `libredlab.a`; it is not an independently bootable OS kernel.
- Key Vault host concurrency tests and RedLab standalone tests passed in prior CI run [43843019649](https://github.com/110we/corecome-out/actions/runs/43843019649).
- PR #5 remains open and unmerged. The latest const-correctness source adjustment had no known subsequent CI evidence at the time of this report.
- Key Vault IRQ/preemption/SMP locking integration and verifier callback/context lifetime remain open risks.

## Next implementation sequence for OpenKernel

1. Add platform discovery and a host-tested FDT parser.
2. Add physical memory discovery, reserved-region handling and page allocator.
3. Add exception vectors, frame contract and interrupt-controller abstraction.
4. Add virtual memory and per-address-space page tables.
5. Implement scheduler, task lifecycle and context switching.
6. Add user-mode transition and a stable syscall ABI.
7. Implement IPC and the requested kernel-native Direct Port Communication: paired endpoints, direct bidirectional channel, no bridge/relay, flow control and safe teardown.
8. Add driver model, network stack, storage/VFS, and extensible module ABI.
9. Add concurrency, fault-injection, QEMU and integration regression tests.

Each item is marked complete only after code and automated evidence exist.