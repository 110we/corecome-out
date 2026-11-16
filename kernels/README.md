# Kernel product lines

- `COXVBN/`: foundational AArch64 kernel, built as its own boot image.
- `RedLab/`: separately buildable authorized assessment and privacy-profile library.

COXVBN owns architecture, boot, memory, platform support, core execution/exception handling and shared kernel security primitives. RedLab owns its scope policy and profile transitions. RedLab's platform authority callback is an explicit integration seam; there is no reverse source dependency from COXVBN into RedLab.

RedLab is an independent library build, not an independently bootable OS kernel. In-kernel execution requires a future stable module ABI, loader, syscall/capability boundary and runtime isolation.
