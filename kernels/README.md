# Kernel product lines

- `COXVBN/`: foundational AArch64 kernel, built as its own boot image.
- `RedLab/`: separately buildable authorized assessment and privacy-profile library; it is not an OS kernel.
- `OpenKernel/`: third, independent, general-purpose AArch64 kernel with its own boot path, linker layout, source tree, and build target.

OpenKernel does not inherit COXVBN capability masks, RedLab target-scope policy, or daily-profile restrictions. It owns its own interfaces and extension policy. Fundamental kernel correctness protections (memory safety, privilege separation, device ownership, resource lifetime) remain part of a reliable kernel.

Build OpenKernel independently with `make -C kernels/OpenKernel`. Its current code is only a boot/serial-console foundation; the scheduler, MMU, processes, syscalls, IPC, direct port communication, drivers, networking and filesystem remain planned. Its initial independent build and QEMU serial boot passed CI run [43846739631](https://github.com/110we/corecome-out/actions/runs/43846739631).

The three product lines must not acquire accidental source, link-time, or runtime dependencies on one another.
