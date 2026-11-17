# OpenKernel — third independent kernel

OpenKernel is the third kernel product in this repository. It is a standalone, general-purpose, extensible AArch64 kernel. It is not a mode, plugin, or sub-library of COXVBN or RedLab.

## Product boundary

- Own boot entry, linker layout, kernel image, platform layer, memory manager, scheduler, drivers, IPC, and user ABI.
- Own build and test targets. It must be buildable without compiling or linking COXVBN or RedLab.
- No inherited COXVBN capability masks, RedLab target-scope policy, daily-profile rules, or product-specific functional allowlists.
- Feature policy is selected by the OpenKernel owner/operator and extension modules; the base kernel does not impose the other products' workflows.
- Ordinary kernel integrity requirements remain: memory safety, privilege separation, explicit device ownership, and reliable recovery. “Open” means functionally unrestricted by the other two products, not deliberately unsafe.

## Current implementation slice

This directory currently contains only a minimal AArch64 boot/serial-console foundation. It is not yet a complete operating system. Scheduler, virtual memory, process isolation, syscalls, networking, drivers, filesystem, and direct port communication are planned and must not be described as implemented until code and tests exist.

## Build

From this directory:

```sh
make
```

The default toolchain prefix is `aarch64-linux-gnu-`; override with `make CROSS_COMPILE=<prefix>`. The independent build and QEMU serial boot test passed in CI run [43846739631](https://github.com/110we/corecome-out/actions/runs/43846739631).

## Design rule

OpenKernel owns its source tree and interfaces. Shared documentation may describe the three products, but OpenKernel must not acquire build-time or runtime dependencies on COXVBN or RedLab.
