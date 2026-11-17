# COXVBN RedLab — authorized penetration-testing environment

## Purpose

RedLab is the security-assessment environment planned on top of the `rewrite/corecome-from-zero` kernel. Its primary use is authorized penetration testing of systems that the operator owns or has explicit permission to assess.

**Architectural rule:** assessment tools run in isolated user space. The kernel provides enforceable boundaries: process isolation, capability checks, scoped network access, resource limits, and audit records.

## Functional areas

1. **Scope and asset inventory** — explicit target CIDRs/domains, ports, exclusions, engagement window, owner/contact, and authorization reference.
2. **Discovery and enumeration** — bounded host discovery and service/protocol identification, restricted to approved targets.
3. **Configuration assessment** — TLS settings, exposed services, security headers, and protocol configuration checks.
4. **Web/API assessment** — safe input validation, access-control and session checks, API schema/configuration review, and non-destructive test cases.
5. **Controlled vulnerability validation** — verify a finding with the least invasive proof possible; destructive checks require separate explicit approval and are not enabled by default.
6. **Evidence and reporting** — timestamped findings, reproducible evidence, severity rationale, remediation guidance, and exportable reports.

## Mandatory safety and enforcement model

- **Default deny:** no target is reachable by assessment tools until it is included in an approved scope.
- **Scope binding:** every job is bound to an immutable scope revision; DNS resolution and redirects must be re-checked against that scope before connecting.
- **Capability separation:** discovery, passive capture, and active probing are separate capabilities. A module receives only the minimum grants required; no elevated grant is implicit. Daily Profile and RedLab capability masks are mutually exclusive.
- **Rate and resource limits:** per-target request rate, concurrency, time budget, and byte budget are enforced before traffic is emitted.
- **Explicit active-test switch:** active checks are off by default and require a per-engagement opt-in.
- **Audit trail:** record operator identity, authorization reference, scope revision, requested action, target, timestamps, result, and stop reason. Secrets and raw credentials must not be written to logs.
- **Global stop:** a kernel-controlled kill switch cancels queued and running jobs and blocks new assessment traffic.
- **Isolation:** assessment tools run as unprivileged processes with no direct MMIO, arbitrary kernel memory, or unrestricted raw-device access.
- **No stealth or persistence:** no evasion, covert persistence, credential harvesting, autonomous propagation, or out-of-scope pivoting.

## Relationship to the Daily Privacy Profile

RedLab and the Daily Privacy Profile are separate security domains, not two labels for the same mode. Daily privacy settings cannot grant assessment permissions. Entering RedLab requires explicit authorization and a scoped engagement; leaving RedLab requires stopping jobs, revoking assessment capabilities, and recording the transition. See [docs/privacy-mode.md](privacy-mode.md) for the daily profile contract and transition requirements.

## Planned architecture

```text
RedLab user space
  ├── Scope Manager          approved targets, exclusions, engagement window
  ├── Discovery              bounded asset and service inventory
  ├── Assessors               TLS / web / API / configuration checks
  ├── Evidence Store          redacted, timestamped findings
  └── Report Engine           remediation-focused output
          │ narrow syscall / IPC requests
          ▼
Kernel security boundary
  ├── Security Plugin         policy decision and capability enforcement
  ├── Scope Guard             target, port, time, and rate validation
  ├── Audit Recorder          append-only event interface
  ├── Network Device Model    scoped sockets; no raw access by default
  └── Module Registry         module identity, capability grants, revocation
          │
          ▼
     Network stack / drivers
```

The diagram is a target architecture, not a claim that these components already exist. The production-facing in-kernel policy entrypoint is `coxvbn_redlab_policy_check_current()`: it resolves the current module only through the kernel execution-context provider, checks the exact discovery, passive-capture, or active-probe capability, and then runs the R1 scope/rate/concurrency/time/byte-budget checks. The scope-only policy helper and explicit-module test adapter are compiled only for host tests; the production kernel does not export a direct scope-only admission path. Allow and deny audit events carry the canonical registry module ID/generation and requested operation. Missing provider, missing grant, wrong domain, pending or revoked module, invalid operation, and operation/active-flag mismatch fail closed. Daily Profile modules cannot request RedLab capabilities, and RedLab modules cannot request Daily data/settings capabilities. The provider remains an interface contract only: there is no scheduler-owned current-task provider, trusted syscall/network boundary, preemption/IRQ-aware locking, user mode, syscall ABI, network stack, persistent audit storage, or production cryptographic backend.

## R1 policy-core status

The R1 policy library is implemented in `kernel/redlab_policy.c` and host-tested. It supports canonical IPv4/IPv6 CIDR matching, allow/exclusion precedence, port ranges, monotonic engagement windows, default-deny decisions, active-test default-off, per-target request/concurrency limits, per-job duration/byte budgets, audit callbacks, and a sticky global stop. R2 adds a single production admission path that applies the operation-specific module capability gate before invoking those R1 checks. It performs no DNS resolution or network I/O. The execution-context provider is not yet backed by a scheduler/current-task implementation; registry and policy synchronization is not yet integrated with preemption/IRQ control. See `docs/redlab-policy-core.md` and `docs/module-registry.md`.

## Delivery stages

- **R0 — design and boundaries:** document threat model, scope schema, capability classes, audit event schema, and kill-switch semantics.
- **R1 — policy core:** implement and host-test CIDR/port/time scope checks, deny-by-default behavior, rate limiting, and audit callbacks without sending network traffic.
- **R2 — kernel interface integration:** host-tested domain-separated module identity/capability admission, per-operation least privilege, revocation, audit identity propagation, and the single production policy entrypoint are implemented. Scheduler-backed identity, preemption/IRQ-safe locking, persistent audit storage, and real network/syscall enforcement remain open.
- **R3 — isolated user space:** implement a minimal RedLab process and narrow syscall/IPC interface after address-space isolation and scheduler work.
- **R4 — passive assessment:** add local inventory and passive protocol/configuration analysis.
- **R5 — controlled active assessment:** add explicitly authorized, rate-limited checks against lab targets; test out-of-scope denial and emergency stop in QEMU.
- **R6 — reporting and hardening:** evidence integrity, redaction, reproducibility, and security regression suite.

## Acceptance criteria

A stage is complete only when automated tests demonstrate:
- out-of-scope destinations are denied, including redirects and DNS changes;
- missing or revoked capabilities deny the operation;
- rate, time, and resource limits cannot be bypassed by a caller;
- every allow/deny decision produces an audit event;
- the global stop blocks new traffic and cancels active work;
- the kernel remains isolated from untrusted assessment plugins.

Until R1–R5 are implemented and tested, RedLab is an architecture plan only. It must not be advertised as a working penetration-testing platform.
