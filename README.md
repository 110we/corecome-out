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

The current host-tested R1/R2 foundation includes the scope/resource policy core, domain-separated module capabilities, and one production-facing RedLab admission entrypoint: `coxvbn_redlab_policy_check_current()`. It obtains module identity only from the kernel execution-context provider, checks the operation-specific discovery, passive-capture, or active-probe grant, then applies R1 scope/rate/budget policy. The raw scope-only and explicit-module helpers are test-only and are not compiled into the production kernel. Daily Profile modules cannot request RedLab capabilities, and RedLab modules cannot request Daily data/settings capabilities; no elevated grants are implicit. These are tested kernel-interface contracts, not yet a runtime security boundary: a scheduler-owned trusted provider, preemption/IRQ-aware locking, user mode, syscalls, storage isolation, and network integration remain unimplemented.

RedLab assessment architecture: [docs/penetration-testing.md](docs/penetration-testing.md). Daily privacy profile contract: [docs/privacy-mode.md](docs/privacy-mode.md).

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
