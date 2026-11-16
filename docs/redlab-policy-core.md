# RedLab R1 policy core

This is the first host-testable R1 slice. It is a pure policy library: no DNS resolution, sockets, packet transmission, or active assessment execution.

- Canonical IPv4/IPv6 CIDR prefixes; malformed prefixes and non-zero host bits are rejected.
- Allow rules bind a CIDR, inclusive port range, and monotonic window (not-before inclusive, expiry exclusive). Matching exclusions always win; no allow match means deny.
- Active requests are denied unless explicitly enabled at policy initialization.
- Per-target request-per-second and concurrent-job limits; per-job duration and cumulative byte budgets.
- Monotonic clock rollback is denied. Every allow/deny decision reaches the audit callback. Global stop is sticky, audited, releases tracked jobs, and denies later checks until reinitialization.

The owner must serialize calls. The audit callback must be nonblocking and must not re-enter the policy. A future kernel adapter must supply trusted module identity, job IDs, monotonic time, and measured byte counts. Kernel locking, capability binding, persistent audit storage, DNS/redirect revalidation, and network integration remain future work.

Host tests cover IPv4/IPv6 CIDR, exclusions, ports, time windows, default-off active tests, rate/concurrency, job limits, audit, and global stop.
