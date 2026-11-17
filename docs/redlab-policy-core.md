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


## Current execution-context gate

The production RedLab admission entry now obtains the module reference only through the kernel execution-context provider. If no provider is installed, or the provider cannot return a nonzero module ID and generation, admission fails closed and the audit record carries zero identity. The provider is install-once during boot and must be backed by trusted per-CPU scheduler state when a scheduler exists. It is not installed by the current boot path, because no scheduler/current-task implementation exists yet.

The explicit-module admission wrapper is compiled only with COXVBN_REDLAB_TESTING. It is a host-test hook, not a production syscall or user-facing API. The provider callback and installation function are kernel-internal contracts; they must never be exposed to user space. Host tests verify unbound denial, successful provider-backed admission, and rejection of a second provider installation. This is interface hardening, not proof of runtime task isolation.


## Registry and Key Vault lock ordering

Module activation and revocation use explicit transitional states. The registry lock protects only short metadata transitions. Proof-of-possession verification/enrollment and Key Vault revocation execute after releasing the registry lock, avoiding callback re-entry deadlocks. While activation or revocation is in progress, the module is not ACTIVE and authorization fails closed. Failed enrollment returns the record to PENDING; failed key revocation restores ACTIVE because the Key Vault reports that its key remains active.

Host coverage includes a proof-verifier callback that re-enters the registry and observes ACTIVATING, demonstrating that the registry lock is not held across that callback. This remains a bounded in-memory Key Vault contract, not a persistent or hardware-backed key store.


## R2 single-entry admission

The production admission entrypoint is `coxvbn_redlab_policy_check_current()`. It obtains the current module reference only from the install-once execution-context provider, resolves the canonical registry identity for audit, maps the requested operation to exactly one capability, rejects pending/inactive/revoked or wrong-domain modules, and only then invokes the R1 scope/rate/concurrency/time/byte-budget evaluator. Discovery, passive capture, and active probing are independent grants. An active-probe request must also carry the active request flag; non-active operations must not carry it.

The raw scope-only function and explicit-module adapter are available only when `COXVBN_REDLAB_TESTING` is defined. The production kernel build omits the scope-only symbol, and CI checks this with `nm`. A dedicated host test is compiled without the test macro and exercises only the current-context entrypoint. It covers unbound and unavailable providers, pending and revoked modules, least-privilege grants, all three operation classes, cross-domain denials, invalid operations/requests, audit identity and operation fields, and sticky global stop through the combined entrypoint.

This remains a kernel-interface contract, not a trusted runtime boundary. The loader authority pointer is not a hardware boundary, and the execution-context provider is not yet backed by a scheduler-owned per-CPU current-task source. Kernel locking is not yet preemption/IRQ-aware; audit is an in-memory callback, not durable storage. No network I/O or scan runner is implemented.
