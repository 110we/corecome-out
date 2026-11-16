# RedLab R1 policy core

This is the first host-testable R1 slice. It is a pure policy library: no DNS resolution, sockets, packet transmission, or active assessment execution.

- Canonical IPv4/IPv6 CIDR prefixes; malformed prefixes and non-zero host bits are rejected.
- Allow rules bind a CIDR, inclusive port range, and monotonic window (not-before inclusive, expiry exclusive). Matching exclusions always win; no allow match means deny.
- Active requests are denied unless explicitly enabled at policy initialization.
- Per-target request-per-second and concurrent-job limits; per-job duration and cumulative byte budgets.
- Monotonic clock rollback is denied. A non-null audit callback is required at initialization, so every allow/deny decision reaches the audit callback. Global stop is sticky, audited, releases tracked jobs, and denies later checks until reinitialization.

The RedLab policy owner must serialize calls. The audit callback must be nonblocking and must not re-enter the policy. The policy accepts an explicit platform-neutral authority callback that supplies a trusted module ID and generation for the requested operation. If the callback is absent, denies the request, or returns an empty identity, the policy fails closed and audits the denial. This callback is an integration seam only: it is not itself a source of trusted identity, and the standalone library does not link COXVBN's module registry or execution-context provider.

The RedLab library has no source or link-time dependency on COXVBN. Its independent package build produces `libredlab.a` and runs policy/profile host tests using `make -C kernels/RedLab clean all test`. The COXVBN module registry remains a separate kernel contract. A future adapter must bind the authority callback to scheduler-owned current-task identity and registry capability checks before any in-kernel or network-facing use.

Host tests cover IPv4/IPv6 CIDR, exclusions, ports, time windows, default-off active tests, rate/concurrency, job limits, callback-provided audit identity/operation, and global stop. They do not prove trusted kernel identity, runtime isolation, or actual network enforcement.

## Module registry synchronization

The module registry now serializes registration, activation, identity resolution, active-state checks, capability authorization, and revocation with a C11 acquire/release atomic lock. Host tests exercise concurrent registration from four threads and verify that module IDs remain unique and returned records remain resolvable.

This lock protects registry metadata only. Registry APIs are thread-context-only: they must not be called from interrupt handlers, and the current kernel has no scheduler/preemption-control layer to integrate with yet. Before enabling preemptive scheduling or IRQ callers, add architecture-aware preemption/interrupt exclusion and verify lock ordering with the Key Vault. Trusted caller identity is also not complete: loader authority remains a kernel-owned pointer contract, not a scheduler-derived current-task identity. No user-facing syscall may expose these raw authority parameters.


## COXVBN integration boundary

The earlier direct dependency on COXVBN's execution-context provider and module registry has been removed from the RedLab policy library to make its build independent. No production adapter currently connects the library's authority callback to COXVBN. The adapter must not accept a module identity supplied by an untrusted caller; it must resolve identity from trusted scheduler state and check the registry's current grants and revocation state.

## Registry and Key Vault lock ordering

Module activation and revocation use explicit transitional states. The registry lock protects only short metadata transitions. Proof-of-possession verification/enrollment and Key Vault revocation execute after releasing the registry lock, avoiding callback re-entry deadlocks. While activation or revocation is in progress, the module is not ACTIVE and authorization fails closed. Failed enrollment returns the record to PENDING; failed key revocation restores ACTIVE because the Key Vault reports that its key remains active.

Host coverage includes a proof-verifier callback that re-enters the registry and observes ACTIVATING, demonstrating that the registry lock is not held across that callback. This remains a bounded in-memory Key Vault contract, not a persistent or hardware-backed key store.
