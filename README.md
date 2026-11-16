# COXVBN

COXVBN is an independent operating-system project. The implementation is being rebuilt from a small, testable foundation rather than treating placeholder subsystems as completed features.

## Related repository

The separate COXVBN system project is maintained here: [COXVBN-system](https://github.com/110we/COXVBN-system).

## Current target

- Architecture: AArch64
- Emulator: QEMU virt
- Language: freestanding C and AArch64 assembly
- First milestone: enter the kernel, initialize a serial console, and prove the boot path in CI

## COXVBN RedLab — security assessment and privacy workspace

COXVBN plans two distinct user-facing profiles built on the same isolation and capability foundations:

- **RedLab assessment profile:** authorized penetration testing in explicitly approved scopes. Planned functions include bounded asset discovery, service enumeration, TLS/configuration checks, web/API security checks, controlled validation, evidence capture, and reporting.
- **Daily privacy profile:** ordinary privacy controls such as separate user/app profiles, clear work-versus-test separation, user-visible appearance/profile switching, notification and sensitive-preview controls, permission review, and reduced nonessential telemetry where supported.

These profiles are deliberately separate. Daily privacy mode does not inherit assessment capabilities. Profile changes must be visible, user-authorized, reversible, and auditable. Privacy controls must not hide processes, evade security tools, defeat audit, or impersonate another person or organization. RedLab active tests remain off by default, scoped, rate-limited, logged, and stoppable. Out-of-scope targets are denied by default.

The host-tested foundation includes the COXVBN module capability registry and the separately built RedLab R1 scope/resource policy library. RedLab now exposes a platform-neutral authority callback; no COXVBN adapter is linked to it yet. Daily Profile modules are rejected by the COXVBN registry if they request RedLab capability bits. This is not yet a runtime security boundary: trusted execution-context binding, kernel locking, user mode, syscalls, and network integration remain unimplemented.

RedLab assessment architecture: [docs/redlab/penetration-testing.md](docs/redlab/penetration-testing.md). Daily privacy profile contract: [docs/redlab/privacy-mode.md](docs/redlab/privacy-mode.md).

## Isolated recovery subsystems

The Key Vault, Information Store, and Emergency Recovery interfaces are separate modules. Their current implementation is an in-memory, testable contract only: there is no persistent vault, cryptographic backend, watchdog takeover, network stack, or TCP listener yet. TCP port 3000 is reserved and logically disabled until fault mode is entered. Recovery requests require credential verification and an allowlisted command.

See [docs/kernel/recovery-and-stores.md](docs/kernel/recovery-and-stores.md).

## Directory groups

- `kernels/COXVBN/`: base kernel grouped by architecture, core, memory, platform, security and recovery.
- `kernels/RedLab/`: scope policy and daily privacy-profile extension.
- `tests/COXVBN/`: COXVBN tests; `kernels/RedLab/tests/`: RedLab-owned tests.
- `docs/kernel/` and `docs/redlab/`: product-line documentation.

COXVBN and RedLab now have separate build/test targets. RedLab builds as a standalone host library, not as a bootable OS kernel; it is not linked into the COXVBN image.

## Build

Install an AArch64 cross compiler, binutils, and QEMU, then run:

```sh
make
make test
make run
```

The default `make` builds the COXVBN AArch64 image and the RedLab host library as separate products. `make test` runs both product-line test suites; `make run` boots only COXVBN in QEMU. To build RedLab alone, run `make -C kernels/RedLab clean all test`. The default cross-compiler prefix for COXVBN is `aarch64-linux-gnu-`. Override it with `CROSS_COMPILE=`.

## Project rules

1. A subsystem is implemented only when code and a test demonstrate it.
2. Hardware features are not enabled until their memory map and initialization contract are defined.
3. The kernel owns the final authority over memory, privilege transitions, and device access.
4. Work is staged: finish a coherent implementation slice, then build and test it.
