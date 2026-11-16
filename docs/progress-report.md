# COXVBN Kernel Progress Report

**Report date:** 2026-11-16  
**Repository:** `110we/corecome-out`  
**Working branch:** `rewrite/corecome-from-zero`  
**Baseline commit:** `c0886da1bd95103f87a689bce7e7fb2edb2dee1e`

## Executive summary

The project has a reproducible AArch64 boot foundation and several host-tested kernel contracts for module capabilities, receiver-key lifecycle, RedLab authorization policy, execution identity, and Daily Privacy Profile transitions. The next major engineering boundary is moving these contracts from host-testable code into scheduler-owned, preemption-safe kernel runtime enforcement.

This report distinguishes implemented/tested contracts from runtime capabilities that do not yet exist.

## Current implementation status

| Area | Status | Evidence / limitation |
|---|---|---|
| AArch64 boot foundation | Implemented; CI/QEMU boot checks pass | Cross-build, image checks, exception-vector checks, QEMU serial boot workflow |
| FDT and physical memory foundations | Implemented in part | Host-tested parser and allocator; generalized MMU/page-table work remains |
| Exception and interrupt foundations | Partial | Exception frame/dispatch and GICv2 primitives host-tested; hardware IRQ delivery remains disabled pending runtime validation |
| Module registry and capability domains | Host-tested contract implemented | Bounded registry, domain separation, activation/revocation transitions; not yet scheduler/IRQ/SMP-safe |
| Receiver Key Vault | Host-tested lifecycle implemented | Enrollment, forward-only rotation, revocation; vault itself currently has no internal concurrency lock |
| RedLab policy core | Host-tested contract implemented | IPv4/IPv6 CIDR, port/time scope, rate/concurrency/job budgets, audit callback, sticky stop; no network I/O |
| RedLab identity binding | Host-tested contract implemented | Production admission path obtains identity from fail-closed execution-context provider; real scheduler provider is not installed |
| Daily Privacy Profile | Host-tested state machine implemented | Explicit transition confirmation, generation-bound handles, fail-closed restricted state; no production isolation/UI |
| User mode and syscall boundary | Not implemented | Requires address-space isolation, scheduler and syscall validation |
| RedLab assessment runner and report engine | Not implemented | Requires isolated userspace runner and kernel IPC/syscall boundary |
| Daily privacy controls | Not implemented | Notification preview, permission UI and telemetry preferences remain roadmap items |

## Latest verified repository and CI state

- Latest working-branch commit at report time: [c0886da](https://github.com/110we/corecome-out/commit/c0886da1bd95103f87a689bce7e7fb2edb2dee1e)
- Push workflow: [run 43812921962](https://github.com/110we/corecome-out/actions/runs/43812921962) — **success**
- Pull-request workflow: [run 43812926993](https://github.com/110we/corecome-out/actions/runs/43812926993) — **success**
- Both runs tested commit `c0886da1bd95103f87a689bce7e7fb2edb2dee1e`.
- The latest workflow covered host tests, AArch64 kernel build, image/vector checks and QEMU serial boot checks.
- Changes remain on `rewrite/corecome-from-zero`; `main` has not been modified or merged by this work.

## Important open risks

1. **Key Vault concurrency:** `kernel/key_vault.c` currently mutates the receiver-key table without an internal lock. Adding a lock naively around proof-verifier callbacks could deadlock on re-entry. The implementation needs bounded state transitions, short critical sections, and callback execution outside the lock.
2. **Registry lock integration:** the registry currently uses an acquire/release spinlock and documents thread-context-only use. IRQ masking, preemption control and SMP/per-CPU integration are not yet present.
3. **Execution identity:** the provider contract fails closed, but the scheduler/current-task provider is not installed. A caller-supplied identity is not an acceptable production security boundary.
4. **Crypto and persistence:** proof verification is injected through a callback. No audited production cryptographic backend, protected persistent key store or anti-rollback storage is claimed.
5. **Profile isolation:** profile handles validate state/domain/generation but do not yet establish address-space, storage or process isolation.
6. **Networking:** RedLab policy code does not resolve DNS, open sockets, send packets or execute scans.

## Next engineering sequence

1. Add Key Vault receiver-table concurrency control using explicit transient states; keep verifier callbacks outside critical sections and add re-entrant/concurrency regression tests.
2. Define the scheduler-owned task control block and lifecycle states without claiming a scheduler exists.
3. Bind current execution identity to the scheduler's trusted per-CPU current-task state.
4. Add preemption/IRQ-aware locking and SMP rules before enabling concurrent privileged entry paths.
5. Integrate audited cryptography and persistent anti-rollback key storage.
6. Only then connect RedLab and Daily Privacy Profile contracts to real kernel service boundaries.
7. After address-space isolation and syscall/IPC exist, build the isolated RedLab assessment runner, evidence store and report generator.

## Completion criteria

A roadmap item is marked complete only when implementation and automated evidence exist. Host tests demonstrate the behavior of the isolated contract; they do not by themselves prove production runtime isolation, hardware concurrency safety, cryptographic assurance or network capability.
