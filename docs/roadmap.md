# Roadmap

## Milestone 1 — reproducible boot

- [x] AArch64 entry assembly and linker layout
- [x] BSS initialization and bootstrap stack
- [x] PL011 serial output
- [ ] Cross-compile in CI
- [ ] QEMU serial boot assertion in CI

## Milestone 2 — platform description and memory map

- Parse the QEMU-provided device tree.
- Validate RAM and reserved regions.
- Add checked physical/virtual address types and a physical page allocator.
- Keep the MMU disabled until page tables and device mappings are tested.

## Milestone 3 — exceptions and interrupts

- Define a complete exception frame and vector entry/exit contract.
- Add GICv2/v3 support for the selected QEMU machine.
- Add a generic interrupt-controller interface and timer driver.

## Milestone 4 — execution

- Add per-CPU state, kernel threads, context switching, and a scheduler.
- Introduce user mode only after address-space isolation and syscall validation exist.

## Milestone 5 — kernel services

- VFS and an initial in-memory filesystem.
- Device model and driver lifecycle.
- IPC and capability-based resource handles.
- Central Security Plugin request interface backed by kernel-enforced policy.

## Milestone 6 — userspace and compatibility

- Minimal init process and shell.
- Native system-call ABI.
- POSIX compatibility as a separately tested layer.

A checkbox is checked only when implementation and automated evidence exist.
