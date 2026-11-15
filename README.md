# COXVBN

COXVBN is an independent operating-system project. The implementation is being rebuilt from a small, testable foundation rather than treating placeholder subsystems as completed features.

## Current target

- Architecture: AArch64
- Emulator: QEMU virt
- Language: freestanding C and AArch64 assembly
- First milestone: enter the kernel, initialize a serial console, and prove the boot path in CI

## Build

Install an AArch64 cross compiler, binutils, and QEMU, then run:

```sh
make
make run
```

The default cross-compiler prefix is `aarch64-linux-gnu-`. Override it with `CROSS_COMPILE=`.

## Project rules

1. A subsystem is implemented only when code and a test demonstrate it.
2. Hardware features are not enabled until their memory map and initialization contract are defined.
3. The kernel owns the final authority over memory, privilege transitions, and device access.
4. Work is staged: finish a coherent implementation slice, then build and test it.

See [docs/architecture.md](docs/architecture.md) and [docs/roadmap.md](docs/roadmap.md).
