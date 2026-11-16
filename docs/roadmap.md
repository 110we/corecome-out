# Roadmap

## Milestone 1 — reproducible boot
- [x] AArch64 entry assembly and linker layout
- [x] BSS initialization and bootstrap stack
- [x] PL011 serial output
- [x] Cross-compile in CI
- [x] QEMU serial boot assertion in CI

## Milestone 2 — platform description and memory map
- [x] Preserve the bootloader-provided FDT pointer across BSS initialization
- [x] Validate FDT header, version, and structure/string block bounds
- [x] Walk FDT structure tokens with bounded depth and property/string checks
- [x] Read memory-node reg ranges and reservation-map entries
- [x] Validate direct reserved-memory child `reg` ranges using the container's address/size cell widths (host-tested)
- [x] Add a bounded bitmap physical page allocator with reservation handling
- [ ] Add checked physical/virtual address types
- [x] Add initial AArch64 EL1 identity mappings for QEMU virt RAM, GIC, and PL011
- [ ] Generalize MMU mappings for arbitrary FDT-described RAM/device ranges
- [ ] Add page-table allocation, dynamic mapping/unmapping, and translation tests
- [ ] Add per-address-space page tables and enforce user/kernel permissions

## Milestone 3 — exceptions and interrupts
- [x] Install an aligned 16-entry EL1 vector table
- [x] Preserve general-purpose registers and capture ELR/SPSR/ESR/FAR for fatal diagnostics
- [x] Record and report the architectural vector index without clobbering the interrupted register state
- [x] Define and host-test the exception-frame field offsets and payload size
- [x] Implement the exception entry/exit assembly path: 304-byte aligned save frame, ELR/SPSR restore, full GPR restore, and `eret` return
- [ ] Runtime-test exception entry/return with injected synchronous and IRQ exceptions
- [ ] Add safe recoverable synchronous-fault handling
- [x] Add host-tested GICv2 quiescent initialization (disable interfaces, read line count, prepare PMR; no IRQ delivery)
- [x] Discover supported GICv2 compatible strings and distributor/CPU-interface `reg` ranges from FDT; validate and bind after the initial MMU map (host-tested parser; QEMU boot-tested)
- [ ] Add a generic interrupt-controller interface and timer PPI binding/dispatch
- [x] Add EL1 physical Generic Timer frequency/counter access and masked one-shot programming (host-tested conversion; timer IRQ remains masked)
- [x] Add host-tested GICv2 interrupt acknowledge and EOI register primitives (IRQ delivery remains disabled)
- [x] Add bounded IRQ handler registration/dispatch, full-token GICv2 EOI, spurious IRQ handling, and fail-closed unhandled IRQ policy (host-tested; hardware delivery remains disabled)
- [ ] Enable and QEMU-test hardware IRQ delivery after timer and exception-return runtime validation

## Milestone 4 — execution
- [ ] Add per-CPU state, kernel threads, context switching, and a scheduler
- [ ] Introduce user mode only after address-space isolation and syscall validation exist

## Milestone 5 — kernel services
- [ ] VFS and an initial in-memory filesystem
- [ ] Device model and driver lifecycle
- [ ] IPC and capability-based resource handles
- [ ] Implement COXVBN Direct Port Communication: paired endpoints, direct shared-channel data path, no bridge/relay
- [ ] Central Security Plugin request interface backed by kernel-enforced policy
- [x] Define bounded receiver-key enrollment, forward-only rotation, and terminal revocation contract (host-tested)
- [x] Add bounded kernel module registry with requested/granted capability checks and terminal revocation (host-tested)
- [ ] Bind active execution context to module identity and enforce capabilities at every privileged subsystem boundary
- [ ] Integrate audited cryptography and persistent anti-rollback key storage

## Milestone 6 — isolated recovery foundations
- [x] Separate Key Vault, Information Store, and Emergency Recovery module interfaces
- [x] Default-closed recovery channel state and allowlisted command authorization contract
- [ ] Audited cryptographic backend and protected key storage
- [ ] Persistent Information Store and separate storage partition
- [ ] Independent watchdog/recovery environment
- [ ] Network stack and authenticated TCP 3000 listener
- [ ] Explicitly confirmed storage target for destructive reset/format

## Milestone 7 — userspace and compatibility
- [ ] Minimal init process and shell
- [ ] Native system-call ABI
- [ ] POSIX compatibility as a separately tested layer

## Milestone 8 — COXVBN RedLab authorized security assessment
- [ ] Define immutable engagement scope schema, target exclusions, and authorization metadata
- [x] Implement host-tested deny-by-default IPv4/IPv6 CIDR, port, and monotonic-time scope guard (no network I/O)
- [ ] Add separate discovery, passive-capture, and active-probe capability classes
- [x] Add host-tested per-target rate/concurrency limits, per-job time/byte budgets, audit callbacks, and sticky global stop (kernel identity/locking integration remains open)
- [x] Bind host-tested scope/policy admission to active module identity and revocable, domain-separated capabilities (kernel execution-context plumbing remains open)
- [x] Record canonical module identity and operation in RedLab audit events (host-tested; persistent audit storage remains open)
- [ ] Add isolated user-space assessment runner after scheduler, address-space isolation, and syscall/IPC ABI
- [ ] Add bounded discovery, service enumeration, TLS/configuration, and web/API assessment tools
- [ ] Add evidence redaction, report generation, and QEMU lab regression tests

## Milestone 9 — Daily Privacy Profile
- [x] Define a host-tested profile identity, generation-bound handles, and domain-specific handle validation contract (cross-profile data isolation remains open)
- [x] Add host-tested profile-scoped handles; daily profile cannot inherit RedLab domain authority
- [x] Implement a host-tested fail-closed transition controller (trusted authentication/UI and runtime revocation integration remain open)
- [ ] Add lock-screen/notification sensitive-preview controls without suppressing security alerts
- [ ] Add permission review and revocation UI
- [ ] Add explicit telemetry preferences with mandatory security-event exceptions
- [ ] Test fail-closed transitions, audit retention, and profile data isolation

A checkbox is checked only when implementation and automated evidence exist.
