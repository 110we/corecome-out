# COXVBN RedLab — authorized penetration-testing environment

## Purpose

RedLab is the security-assessment environment planned on top of the `rewrite/corecome-from-zero` kernel. Its primary use is authorized penetration testing of systems that the operator owns or has explicit permission to assess.

**Architectural rule:** offensive assessment tools run in isolated user space. The kernel does not become a bag of privileged scanning or exploitation routines. The kernel provides enforceable boundaries: process isolation, capability checks, scoped network access, resource limits, and audit records.

## Functional areas

1. **Scope and asset inventory** — explicit target CIDRs/domains, ports, exclusions, engagement window, owner/contact, and authorization reference.
2. **Discovery and enumeration** — bounded host discovery and service/protocol identification, restricted to approved targets.
3. **Configuration assessment** — TLS settings, exposed services, security headers, and protocol configuration checks.
4. **Web/API assessment** — safe input validation, access-control and session checks, API schema/configuration review, and non-destructive test cases.
5. **Controlled vulnerability validation** — verify a finding with the least invasive proof possible; destructive checks require a separate explicit approval and are not enabled by default.
6. **Evidence and reporting** — timestamped findings, reproducible evidence, severity rationale, remediation guidance, and exportable reports.

## Mandatory safety and enforcement model

- **Default deny:** no target is reachable by assessment tools until it is included in an approved scope.
- **Scope binding:** every job is bound to an immutable scope revision; DNS resolution and redirects must be re-checked against that scope before connecting.
- **Capability separation:** discovery, passive capture, and active probing are separate capabilities. A module receives only the minimum grants required.
- **Rate and resource limits:** per-target request rate, concurrency, time budget, and byte budget are enforced before traffic is emitted.
- **Explicit active-test switch:** active checks are off by default and require a per-engagement opt-in.
- **Audit trail:** record operator identity, authorization reference, scope revision, requested action, target, timestamps, result, and stop reason. Secrets and raw credentials must not be written to logs.
- **Global stop:** a kernel-controlled kill switch cancels queued and running jobs and blocks new assessment traffic.
- **Isolation:** assessment tools run as unprivileged processes with no direct MMIO, arbitrary kernel memory, or unrestricted raw-device access.
- **No stealth or persistence:** no evasion, covert persistence, credential harvesting, autonomous propagation, or out-of-scope pivoting.

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

The diagram is a target architecture, not a claim that these components already exist. The current kernel has a module registry and capability-mask contract, but it does not yet have user mode, a syscall ABI, a network stack, persistent audit storage, or a production cryptographic backend.

## Delivery stages

- **R0 — design and boundaries:** document threat model, scope schema, capability classes, audit event schema, and kill-switch semantics.
- **R1 — policy core:** implement and host-test CIDR/port/time scope checks, deny-by-default behavior, rate limiting, and audit callbacks without sending network traffic.
- **R2 — kernel integration:** bind the policy core to trusted execution identity and module capabilities; add revocation and global stop semantics.
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
