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
- [ ] Validate reserved-memory child nodes and all parent cell-width combinations
- [x] Add a bounded bitmap physical page allocator with reservation handling
- [ ] Add checked physical/virtual address types
- [x] Add initial AArch64 EL1 identity mappings for QEMU virt RAM, GIC, and PL011
- [ ] Generalize MMU mappings for arbitrary FDT-described RAM/device ranges
- [ ] Add page-table allocation, dynamic mapping/unmapping, and translation tests
- [ ] Add per-address-space page tables and enforce user/kernel permissions

## Milestone 3 — exceptions and interrupts
- [x] Install an aligned 16-entry EL1 vector table
- [x] Preserve general-purpose registers and capture ELR/SPSR/ESR/FAR for fatal diagnostics
- [ ] Define and test the complete exception frame and vector entry/exit contract
- [x] Decode common ESR_EL1 exception classes for fatal diagnostics
- [ ] Add safe recoverable synchronous-fault handling
- [ ] Add GICv2 support for the selected QEMU machine
- [ ] Add a generic interrupt-controller interface and architectural timer driver
- [ ] Add IRQ dispatch, interrupt acknowledgement/EOI, and interrupt-context rules

## Milestone 4 — execution
- [ ] Add per-CPU state, kernel threads, context switching, and a scheduler
- [ ] Introduce user mode only after address-space isolation and syscall validation exist

## Milestone 5 — kernel services
- [ ] VFS and an initial in-memory filesystem
- [ ] Device model and driver lifecycle
- [ ] IPC and capability-based resource handles
- [ ] Implement COXVBN Direct Port Communication: paired endpoints, direct shared-channel data path, no bridge/relay
- [ ] Central Security Plugin request interface backed by kernel-enforced policy

## Milestone 6 — isolated recovery foundations
- [x] Separate Key Vault, Information Store, and Emergency Recovery module interfaces
- [x] Default-closed recovery channel state and allowlisted command authorization contract
- [ ] Audited cryptographic backend and protected key storage
- [ ] Persistent Information Store and separate storage partition
- [ ] Independent watchdog/recovery environment
- [ ] Network stack and authenticated TCP 3000 listener
- [ ] Explicitly confirmed storage target for destructive reset/format

A checkbox is checked only when implementation and automated evidence exist.

## Milestone 7 — userspace and compatibility
- [ ] Minimal init process and shell
- [ ] Native system-call ABI
- [ ] POSIX compatibility as a separately tested layer
