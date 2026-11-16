# COXVBN RedLab extension

- `policy/`: explicit CIDR/port/time scope, exclusions, rate/concurrency/resource limits, default-off active checks, audit callback and global stop. Host-tested; no network I/O.
- `profile/`: Daily/RedLab profile transition and capability-domain separation. Host-tested transition contract.

Planned user-space groups (scope manager, bounded discovery, assessors, evidence and reporting) are documented but are not executable tools yet.

**Build boundary:** the current COXVBN image links the RedLab policy/profile objects into the same kernel image for host-testable integration. RedLab is separately organized but is not yet an independently bootable kernel image. Shared enforcement primitives remain in COXVBN; do not duplicate them.

Only explicitly authorized assessment is in scope. Active checks remain opt-in, scoped, rate-limited, audited and stoppable. No stealth, covert persistence, evasion, or out-of-scope activity.
