# COXVBN RedLab — independent extension build

RedLab is a separately buildable authorized security-assessment policy and daily privacy-profile library. It is not a second OS kernel or a bootable image.

## Independent build

From the repository root:

```sh
make -C kernels/RedLab clean all test
```

This builds `kernels/RedLab/build/libredlab.a` and runs RedLab-owned policy and profile test binaries. It uses only the C standard library and package-owned interfaces; it does not include COXVBN kernel headers or link COXVBN object files.

## Groups

- `policy/`: target scope, exclusions, time windows, rate/concurrency/resource budgets, audit, global stop and injected authority callback.
- `profile/`: Daily/RedLab profile transition contract with explicit authorization, revocation and fail-closed audit callbacks.
- `tests/`: RedLab-only host tests.

## Integration boundary

The RedLab policy accepts a host/platform-neutral authority callback. A future COXVBN adapter may implement that callback using trusted scheduler identity and the module capability registry, but that adapter is not linked into the independent RedLab library today.

COXVBN does not link RedLab into its boot image. Running RedLab inside COXVBN requires a future stable module ABI, loader, syscall/capability boundary and runtime isolation; these are not claimed as implemented.

Only explicitly authorized assessment is in scope. Active checks remain opt-in, scoped, rate-limited, audited and stoppable. No stealth, covert persistence, evasion, or out-of-scope activity.
