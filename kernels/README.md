# Kernel product lines

- `COXVBN/`: foundational AArch64 kernel and shared enforcement primitives.
- `RedLab/`: separately grouped authorized security-assessment policy and daily privacy-profile extension.

COXVBN owns architecture, boot, memory, platform support, core execution/exception handling, shared security primitives and recovery contracts. RedLab owns its assessment scope policy and profile transitions. Shared low-level code is referenced through interfaces, not copied.

RedLab is not an independent bootable image yet; its current policy/profile objects are linked into the COXVBN development image.
