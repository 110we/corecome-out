# COXVBN project family

This repository contains three distinct product lines:

1. **COXVBN** — foundational AArch64 kernel.
2. **RedLab** — authorized security-assessment and privacy-profile library. It is not a bootable kernel.
3. **OpenKernel** — third independent, general-purpose AArch64 kernel. It is not a mode or extension of the first two.

## OpenKernel

OpenKernel has its own boot entry, linker layout, platform code, build system and subsystem roadmap. It does not inherit COXVBN capability masks, RedLab target-scope rules, or daily-profile restrictions. See [the OpenKernel architecture and completion plan](docs/third-kernel/open-kernel.md).

Current OpenKernel implementation is an initial boot/serial-console foundation. Its standalone AArch64 build and QEMU serial boot test passed CI run [43846739631](https://github.com/110we/corecome-out/actions/runs/43846739631). Build it separately with:

```sh
make -C kernels/OpenKernel
```

## Existing COXVBN and RedLab

COXVBN remains the foundational AArch64 kernel. RedLab remains a separately built library for authorized security assessment and privacy-profile contracts. Their current status and build instructions are documented in their respective directories and the development progress report.

## Build

Install an AArch64 cross compiler, binutils, and QEMU, then run:

```sh
make
make test
make run
```

The root build continues to build COXVBN and RedLab. OpenKernel has its own independent build target: `make openkernel`; to boot it in QEMU, run `make run-openkernel`.

## Project rules

1. A subsystem is implemented only when code and a test demonstrate it.
2. Hardware features are not enabled until their memory map and initialization contract are defined.
3. Each kernel owns its own authority, ABI, build and runtime boundaries.
4. Work is staged: finish a coherent implementation slice, then build and test it.
