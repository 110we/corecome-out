# RedLab R1 policy core

This is the first host-testable R1 slice. It is a pure policy library: no DNS resolution, sockets, packet transmission, or active assessment execution.

- Canonical IPv4/IPv6 CIDR prefixes; malformed prefixes and non-zero host bits are rejected.
- Allow rules bind a CIDR, inclusive port range, and monotonic window (not-before inclusive, expiry exclusive). Matching exclusions always win; no allow match means deny.
- Active requests are denied unless explicitly enabled at policy initialization.
- Per-target request-per-second and concurrent-job limits; per-job duration and cumulative byte budgets.
- Monotonic clock rollback is denied. A non-null audit callback is required at initialization, so every allow/deny decision reaches the audit callback. Global stop is sticky, audited, releases tracked jobs, and denies later checks until reinitialization.

The RedLab policy owner must serialize calls. The audit callback must be nonblocking and must not re-enter the policy. A future kernel adapter must supply trusted module identity, job IDs, monotonic time, and measured byte counts. Host-tested module-bound admission now checks active module identity and separate discovery, passive-capture, and active-probe capabilities; revoked or inactive modules are denied. Audit schema v2 records the canonical registry module ID/generation and requested operation on authorized-path allow and deny decisions. Unknown references are recorded with zero identity. These fields are resolved from the registry, never copied from request payload fields. Domain-aware registration prevents Daily Profile modules from requesting or receiving RedLab capability bits. Trusted execution-context plumbing, registry/policy synchronization, persistent audit storage, DNS/redirect revalidation, and network integration remain future work.

Host tests cover IPv4/IPv6 CIDR, exclusions, ports, time windows, default-off active tests, rate/concurrency, job limits, audit identity/operation propagation, and global stop.


## Module registry synchronization

The module registry now serializes registration, activation, identity resolution, active-state checks, capability authorization, and revocation with a C11 acquire/release atomic lock. Host tests exercise concurrent registration from four threads and verify that module IDs remain unique and returned records remain resolvable.

This lock protects registry metadata only. Registry APIs are thread-context-only: they must not be called from interrupt handlers, and the current kernel has no scheduler/preemption-control layer to integrate with yet. Before enabling preemptive scheduling or IRQ callers, add architecture-aware preemption/interrupt exclusion and verify lock ordering with the Key Vault. Trusted caller identity is also not complete: loader authority remains a kernel-owned pointer contract, not a scheduler-derived current-task identity. No user-facing syscall may expose these raw authority parameters.
