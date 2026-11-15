# AArch64 Bring-up

## Implemented in this branch

- `boot.S` establishes the EL1 stack, clears BSS and enters `kernel_main`.
- `el.S` accepts EL1 or performs the documented EL2-to-EL1h transition. EL3 is rejected deliberately: a correct EL3 handoff must define `SCR_EL3` security state, exception routing, timer access and firmware ownership rather than guessing.
- `vectors.S` installs a 2 KiB-aligned EL1 vector table with all 16 architectural vector slots. Each class currently enters a fail-stop diagnostic path.
- `exception.c` prints exception class, `ESR_EL1`, `FAR_EL1` and `ELR_EL1` over the early PL011 console.

## Deliberately not enabled yet

- MMU: no page-table allocator, physical memory map, memory attributes or identity mappings exist yet. Enabling `SCTLR_EL1.M` before those are defined would make instruction/data fetches unsafe. The MMU implementation belongs with the physical allocator and mapping code.
- GIC/timer: interrupt-controller version and timer routing must be selected from the QEMU machine description and initialized before unmasking IRQs. Until then exceptions remain masked and unexpected exceptions stop with diagnostics.
- EL0 entry and context switching: the current vector stubs are early-boot fatal handlers, not a complete user/kernel context save/restore path.

## Next implementation gates

1. Parse or define the QEMU virt memory map and reserve kernel/image/stack regions.
2. Add physical page allocator and page-table construction.
3. Configure MAIR/TCR/TTBR and enable MMU only after identity and device mappings are verified.
4. Add GICv2/v3 discovery/configuration and the architectural timer, then enable IRQs.
5. Replace fatal-only vector stubs with full register-frame save/restore before EL0 or preemptive scheduling.