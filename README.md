# COXVBN

COXVBN is an independent operating-system project. The implementation is being rebuilt from a small, testable foundation rather than treating placeholder subsystems as completed features.

## Related repository

The separate COXVBN system project is maintained here: [COXVBN-system](https://github.com/110we/COXVBN-system).

## Current target

- Architecture: AArch64
- Emulator: QEMU virt
- Language: freestanding C and AArch64 assembly
- First milestone: enter the kernel, initialize a serial console, and prove the boot path in CI

## COXVBN RedLab — authorized security assessment environment

The refactored kernel will host a dedicated security-testing environment focused on authorized penetration testing. RedLab is designed as a **user-space assessment suite**, not as privileged attack logic inside the kernel. The kernel supplies isolation, capability checks, scoped network access, and tamper-evident audit hooks.

Planned capabilities include asset discovery within an explicitly approved scope, service and protocol enumeration, TLS/configuration checks, web/API security checks, controlled validation of known weaknesses, evidence capture, and report generation. Active tests must be explicitly enabled for each scope, rate-limited, logged, and stoppable with a global kill switch. Out-of-scope targets are denied by default.

RedLab will not include stealth/persistence, credential theft, uncontrolled exploitation, or autonomous propagation. See [docs/penetration-testing.md](docs/penetration-testing.md) for the architecture and staged implementation plan.

## Isolated recovery subsystems

The Key Vault, Information Store, and Emergency Recovery interfaces are separate modules. Their current implementation is an in-memory, testable contract only: there is no persistent vault, cryptographic backend, watchdog takeover, network stack, or TCP listener yet. TCP port 3000 is reserved and logically disabled until fault mode is entered. Recovery requests require credential verification and an allowlisted command.

See [docs/recovery-and-stores.md](docs/recovery-and-stores.md).

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
