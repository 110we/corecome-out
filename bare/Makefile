# Makefile —— 一条 make 出 ELF，make test 开机并断言 P0 各项。
# .RECIPEPREFIX 用 '>'，省得编辑器把 tab 换成空格。

.RECIPEPREFIX = >

CROSS ?= aarch64-linux-gnu-
CC := $(CROSS)gcc
LD := $(CROSS)ld

BUILD := build
OBJS := $(BUILD)/boot.o $(BUILD)/vectors.o $(BUILD)/switch.o $(BUILD)/kernel.o \
        $(BUILD)/heap.o $(BUILD)/mm.o $(BUILD)/paging.o $(BUILD)/sched.o \
        $(BUILD)/vfs.o $(BUILD)/syscall.o

CFLAGS := -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin \
          -nostdlib -mgeneral-regs-only -O2 -Wall -Wextra -Werror -g
LDFLAGS := -T linker.ld -nostdlib -z max-page-size=4096 -Map=$(BUILD)/kernel.map

QEMU := qemu-system-aarch64
QEMUFLAGS := -M virt,gic-version=2 -cpu cortex-a53 -display none -monitor none
QEMU_EXTRA ?=

.PHONY: all run test test-cmd clean

all: $(BUILD)/kernel.elf

$(BUILD):
> mkdir -p $@

$(BUILD)/%.o: %.S | $(BUILD)
> $(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c | $(BUILD)
> $(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(OBJS) linker.ld
> $(LD) $(LDFLAGS) -o $@ $(OBJS)

run: $(BUILD)/kernel.elf
> $(QEMU) $(QEMUFLAGS) -serial stdio -kernel $<

test: $(BUILD)/kernel.elf
> @rm -f $(BUILD)/serial.log
> @timeout 10 $(QEMU) $(QEMUFLAGS) $(QEMU_EXTRA) -serial file:$(BUILD)/serial.log -kernel $< || true
> @cat $(BUILD)/serial.log
> @grep -q "\[bare\] aarch64 virt" $(BUILD)/serial.log || (echo "FAIL: boot"; exit 1)
> @grep -q "vec: vbar=0x" $(BUILD)/serial.log || (echo "FAIL: vec"; exit 1)
> @grep -q "gic: ready" $(BUILD)/serial.log || (echo "FAIL: gic"; exit 1)
> @grep -q "irq: ticks=.* ok" $(BUILD)/serial.log || (echo "FAIL: timer"; exit 1)
> @grep -q "mmu: sctlr=0x.* on" $(BUILD)/serial.log || (echo "FAIL: mmu"; exit 1)
> @grep -q "fault: caught=1 ok" $(BUILD)/serial.log || (echo "FAIL: fault"; exit 1)
> @grep -q "svc: caught=1 ok" $(BUILD)/serial.log || (echo "FAIL: svc"; exit 1)
> @grep -q "\[sys\] heap ok" $(BUILD)/serial.log || (echo "FAIL: heap"; exit 1)
> @grep -q "\[sys\] syscall ok" $(BUILD)/serial.log || (echo "FAIL: syscall"; exit 1)
> @grep -q "\[kern\] mm: pages=128" $(BUILD)/serial.log || (echo "FAIL: mm"; exit 1)
> @grep -q "\[kern\] slab ok" $(BUILD)/serial.log || (echo "FAIL: slab"; exit 1)
> @grep -q "\[kern\] paging: 4K aspace ok" $(BUILD)/serial.log || (echo "FAIL: paging"; exit 1)
> @grep -q "\[kern\] vfs: hello from ramfs" $(BUILD)/serial.log || (echo "FAIL: vfs"; exit 1)
> @grep -q "\[kern\] rootfs:" $(BUILD)/serial.log || (echo "FAIL: rootfs"; exit 1)
> @grep -q "\[thr\] A 0" $(BUILD)/serial.log || (echo "FAIL: thread A"; exit 1)
> @grep -q "\[thr\] B 0" $(BUILD)/serial.log || (echo "FAIL: thread B"; exit 1)
> @grep -q "\[kern\] sched: done" $(BUILD)/serial.log || (echo "FAIL: sched done"; exit 1)
> @grep -q "BIG_KERNEL_OK" $(BUILD)/serial.log || (echo "FAIL: big kernel"; exit 1)
> @grep -q "ALL_OK" $(BUILD)/serial.log || (echo "FAIL: all"; exit 1)
> @echo "BARE_P0_OK"
> @$(MAKE) -s test-cmd

test-cmd: $(BUILD)/kernel.elf
> @sh tools/serial_cmd.sh "$(QEMU)" "$<" "$(BUILD)/cmd.log"
> @echo "BARE_CMD_OK"

clean:
> rm -rf $(BUILD)
