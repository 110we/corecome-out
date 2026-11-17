# COXVBN Kernel Progress Report

**Report date:** 2026-11-17  
**Repository:** `110we/corecome-out`  
**Working branch:** `rewrite/corecome-from-zero`  
**R2 implementation commit:** [924c51e](https://github.com/110we/corecome-out/commit/924c51e49ea76e960caaa2e346ac3cfb20481abf)  
**CI repair/validation commit:** [423c3fe](https://github.com/110we/corecome-out/commit/423c3fec5538d7ba1def072d463bd2fe034f4678)

## Executive summary

R2 is complete as a **host-tested kernel-interface contract**. The module registry enforces disjoint SYSTEM, DAILY_PROFILE, and REDLAB capability domains; RedLab discovery, passive capture, and active probing require separate grants; and the production-facing admission API combines current-context identity lookup, capability authorization, and the R1 scope/rate/budget evaluator. The raw scope-only and explicit-module test helpers are not included in the production kernel build.

This does **not** claim that trusted runtime identity binding, OS-level privacy isolation, or network assessment is operational. Those require scheduler, privilege, synchronization, syscall, storage, and network foundations that are not yet present.

## Current implementation status

| Area | Status | Evidence / limitation |
|---|---|---|
| AArch64 boot foundation | Implemented; latest QEMU CI passed | Cross-build, ELF/image checks, exception-vector alignment, GICv2 DTB validation, QEMU serial boot |
| R1 RedLab policy | Implemented and host-tested | IPv4/IPv6 CIDR, exclusions, ports, monotonic windows, default deny, active default-off, rate/concurrency/time/byte budgets, audit callback, sticky global stop; no network I/O |
| Module capability registry | R2 interface contract implemented and host-tested | Explicit requested/granted masks, activation, canonical module ID/generation, revocation, domain checks, registry metadata lock; thread-context only |
| RedLab operation admission | R2 implemented and host-tested | Single production entrypoint; distinct discovery/passive/active grants; capability gate runs before R1 policy; direct scope-only production symbol excluded and checked by CI |
| Daily/RedLab domain isolation | R2 implemented and host-tested | Daily modules cannot request RedLab bits; RedLab modules cannot request Daily data/settings bits; no implicit elevated grant |
| Audit | Host-tested callback contract | Allowed/denied admission events carry canonical module identity and operation; storage is in-memory callback only, not durable |
| Daily Privacy Profile | Host-tested state-machine contract | Explicit transition, generation-bound domain handles, stop/revoke and fail-closed transitions; no desktop/UI or OS data isolation |
| Trusted execution identity | Interface only | Test provider exists, but no scheduler-owned current-task provider is installed |
| Kernel synchronization | Partial | Registry spinlock protects metadata in thread context; no preemption/IRQ/SMP integration |
| User mode/syscalls/network | Not implemented | No user-mode boundary, syscall ABI, network stack, RedLab runner, or scan execution |

## R2 delivery details

1. **Least privilege:** each RedLab operation maps to one capability: discovery, passive capture, or active probe. A grant for one operation does not imply either of the others.
2. **Module lifecycle:** PENDING, inactive, or REVOKED modules fail authorization. Revocation removes the grant and is checked again at each admission.
3. **Domain isolation:** DAILY_PROFILE registration is restricted to Daily data/settings bits; REDLAB registration is restricted to RedLab bits. Authorization rechecks domain membership.
4. **Single policy path:** production callers use `coxvbn_redlab_policy_check_current()`. It obtains identity from the execution-context provider, checks the operation-specific capability, and then applies R1 scope, rate, concurrency, time, and byte budgets.
5. **No production bypass:** raw scope-only and explicit-module adapters are guarded by `COXVBN_REDLAB_TESTING`. CI verifies that the scope-only symbol is absent from the AArch64 kernel image.
6. **Audit and stop:** host tests verify canonical module ID/generation and operation in allow/deny records, invalid-call denial, and sticky global stop through the combined admission path.

## Automated validation

The latest successful run is [GitHub Actions run 44010518715](https://github.com/110we/corecome-out/actions/runs/44010518715), for commit `423c3fec5538d7ba1def072d463bd2fe034f4678`.

| CI step | Result |
|---|---|
| Host test suite (`make test-host`) | **Success** |
| AArch64 kernel build (`make clean all`) | **Success** |
| ELF/image and production policy API boundary check | **Success** |
| Exception vector table verification | **Success** |
| QEMU GICv2 device-tree verification | **Success** |
| QEMU serial boot test | **Success** |

The first push attempt, [run 44010432288](https://github.com/110we/corecome-out/actions/runs/44010432288), failed before creating a job because the workflow YAML was malformed. The workflow was repaired in commit `423c3fe`; the subsequent run above completed every step successfully. The repository was not available as a local checkout in this execution environment, so the build/test evidence cited here is the GitHub Actions run, not a separately claimed local run.

## Architecture boundaries and open risks

- The loader-authority pointer is an in-kernel API guardrail, **not a hardware security boundary**.
- The execution-context provider is an interface seam. It is not backed by a scheduler-owned per-CPU current-task source; therefore real runtime identity binding is **not complete and is not claimed**.
- Registry locking is not yet preemption-, IRQ-, or SMP-aware. Current APIs remain thread-context-only.
- Profile handles validate domain and generation but do not create address-space, filesystem, or process isolation.
- Audit callbacks are not persistent or tamper-resistant storage.
- The Key Vault remains an in-memory contract with an injected proof verifier, not an audited production cryptographic backend or protected persistent store.
- There is no syscall/network enforcement boundary, DNS/redirect revalidation, network stack, assessment runner, or network scan implementation.
- Daily privacy features remain planned user-controlled settings; the kernel does not yet provide a desktop, notification controls, permission UI, or telemetry UI.

Daily Privacy Profile remains limited to visible, user-controlled, reversible privacy settings. It must not impersonate a real person or organization, conceal malicious processes or unauthorized network activity, evade EDR/security tools, or suppress audit/security events.

## Completion decision and next slice

**R2 code and automated evidence are complete at the interface-contract level.** R3 and real network assessment have not been started. At the time of this R2 report, work stopped at this boundary. Subsequent VFS and device-model work is tracked below. Any future runtime identity work must first introduce scheduler-owned task identity and preemption/IRQ-safe synchronization, then integrate syscall/network enforcement and durable audit without weakening the existing fail-closed policy path.


## Subsequent kernel foundation work

### Bounded VFS foundation

- Added `kernel/vfs.h` and `kernel/vfs.c`: fixed-capacity in-memory root, directory and file nodes; strict absolute-path parsing; bounded file reads/writes; rejection of traversal and malformed paths.
- Added host tests for path validation, duplicate creation, directory/file type errors, read/write bounds, and zero-length reads.
- Initialized a static VFS instance during boot, created `/etc/kernel.conf`, and added a read-back content integrity check.
- CI run [44019246846](https://github.com/110we/corecome-out/actions/runs/44019246846) passed, including the QEMU serial-boot assertion for the VFS self-test message.
- This remains a small RAM-backed filesystem: no block device, persistence, permissions, mounts, unlink/rename, or crash consistency.

### Device lifecycle registry

- Added a fixed-capacity registry for up to 32 devices, with unique names, classes, generation-bound references, and probe/remove/suspend/resume callbacks.
- Probing devices are kept out of normal lookup until probe succeeds; stale references fail after unregister.
- Added host tests for duplicate names, failed probe, capacity limits, lifecycle transitions, stale references, and lookup visibility during probe.
- In CI run [44019616731](https://github.com/110we/corecome-out/actions/runs/44019616731), all checks passed, including QEMU serial boot. The subsequent report-only commit was also verified by [run 44019721104](https://github.com/110we/corecome-out/actions/runs/44019721104), which passed the complete workflow.
- This is not yet a full device model: bus discovery, hardware matching, firmware, DMA, interrupt binding, and IRQ/SMP-safe registry locking remain open.

The overall kernel completion estimate is unchanged. These additions are foundational slices, not evidence that the complete OS, persistent storage, or end-to-end networking is finished.
