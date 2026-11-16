# COXVBN base kernel

- `arch/aarch64/`: boot entry and linker layout.
- `core/`: kernel entry, execution context and exceptions.
- `memory/`: physical allocator and initial MMU.
- `platform/`: UART, FDT, GICv2 and ARM generic timer.
- `security/`: Key Vault and module registry.
- `recovery/`: information store and emergency recovery.

RedLab-specific scope policy and daily privacy-profile logic live under `kernels/RedLab/`. COXVBN owns shared low-level enforcement primitives; RedLab consumes them through narrow interfaces.

This is an early AArch64 bring-up, not a complete general-purpose OS. Scheduler, user-mode isolation, syscall ABI, network stack and production cryptographic facilities must not be described as implemented until code and tests prove them.
