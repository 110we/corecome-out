# Makefile —— 一条 make 出 ELF，make test 开机并断言 P0 各项。
# .RECIPEPREFIX 用 '>'，省得编辑器把 tab 换成空格。

.RECIPEPREFIX = >

CROSS ?= aarch64-linux-gnu-
CC := $(CROSS)gcc
LD := $(CROSS)ld

BUILD := build
OBJS := $(BUILD)/boot.o $(BUILD)/vectors.o $(BUILD)/switch.o $(BUILD)/kernel.o \
        $(BUILD)/heap.o $(BUILD)/mm.o $(BUILD)/paging.o $(BUILD)/sched.o \
        $(BUILD)/vfs.o $(BUILD)/syscall.o $(BUILD)/user.o $(BUILD)/user_blob.o \
        $(BUILD)/kinfo.o $(BUILD)/proc.o $(BUILD)/virtio_blk.o $(BUILD)/ext2.o \
        $(BUILD)/fdt.o

CFLAGS := -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin \
          -nostdlib -mgeneral-regs-only -O2 -Wall -Wextra -Werror -g
LDFLAGS := -T linker.ld -nostdlib -z max-page-size=4096 -Map=$(BUILD)/kernel.map

# 块设备镜像：ext2，1K 块，关掉 extent（读端只认传统块映射）。
# 没装 e2fsprogs 的机器上跳过这一项，不把整条自检卡死。
HAVE_MKFS := $(shell command -v mke2fs 2>/dev/null)
STAGE := $(BUILD)/rootfs
DISK := $(BUILD)/disk.img
DISKFLAGS := -drive file=$(DISK),if=none,format=raw,id=hd0 \
             -device virtio-blk-device,drive=hd0

QEMU := qemu-system-aarch64
QEMUFLAGS := -M virt,gic-version=2 -cpu cortex-a53 -display none -monitor none
QEMU_EXTRA ?=

.PHONY: all run test test-cmd test-m4 clean

all: $(BUILD)/kernel.bin

$(BUILD):
> mkdir -p $@

$(BUILD)/%.o: %.S | $(BUILD)
> $(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c | $(BUILD)
> $(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(OBJS) linker.ld
> $(LD) $(LDFLAGS) -o $@ $(OBJS)

# QEMU 只在认出 arm64 Image 头时才生成 FDT 并把地址放进 x0；
# 直接给 ELF 的话 x0=0、也没有 DTB（boot.S 里有 Image 头）。
$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
> $(CROSS)objcopy -O binary $< $@

$(STAGE)/hello.txt:
> mkdir -p $(STAGE)
> printf 'hello from ext2\n' > $@

$(DISK): $(STAGE)/hello.txt
> rm -f $@
> @if [ -n "$(HAVE_MKFS)" ]; then \
    mke2fs -q -F -t ext2 -b 1024 -O ^extent -d $(STAGE) $@ 512; \
  else : > $@; fi

run: $(BUILD)/kernel.bin $(DISK)
> $(QEMU) $(QEMUFLAGS) $(DISKFLAGS) -serial stdio -kernel $<

test: $(BUILD)/kernel.bin $(DISK)
> @rm -f $(BUILD)/serial.log
> @timeout 10 $(QEMU) $(QEMUFLAGS) $(DISKFLAGS) $(QEMU_EXTRA) -serial file:$(BUILD)/serial.log -kernel $< || true
> @cat $(BUILD)/serial.log
> @grep -q "\[bare\] aarch64 virt" $(BUILD)/serial.log || (echo "FAIL: boot"; exit 1)
> @grep -q "vec: vbar=0x" $(BUILD)/serial.log || (echo "FAIL: vec"; exit 1)
> @grep -q "dtb: magic=0x00000000d00dfeed ok" $(BUILD)/serial.log || (echo "FAIL: dtb magic"; exit 1)
> @grep -q "dtb: mem=0x0000000040000000+0x0000000008000000" $(BUILD)/serial.log || (echo "FAIL: dtb memory"; exit 1)
> @grep -q "dtb: uart=0x0000000009000000+0x0000000000001000 gicd=0x0000000008000000 gicc=0x0000000008010000" $(BUILD)/serial.log || (echo "FAIL: dtb devices"; exit 1)
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
> @grep -q "hello from EL0" $(BUILD)/serial.log || (echo "FAIL: el0 puts"; exit 1)
> @grep -q "code=0 preempt=[1-9]" $(BUILD)/serial.log || (echo "FAIL: el0 preempt"; exit 1)
> @grep -q "user: syscall denied" $(BUILD)/serial.log || (echo "FAIL: syscall guard"; exit 1)
> @grep -q "user: fault" $(BUILD)/serial.log || (echo "FAIL: el0 isolation"; exit 1)
> @grep -q "user: insn-fault" $(BUILD)/serial.log || (echo "FAIL: el0 UXN"; exit 1)
> @grep -q "proc: fork pid=" $(BUILD)/serial.log || (echo "FAIL: fork"; exit 1)
> @grep -q "exec: hello from child" $(BUILD)/serial.log || (echo "FAIL: exec"; exit 1)
> @grep -q "proc: wait pid=.* code=7" $(BUILD)/serial.log || (echo "FAIL: wait code"; exit 1)
> @grep -q "cow: child wrote" $(BUILD)/serial.log || (echo "FAIL: cow child"; exit 1)
> @grep -q "cow: parent intact" $(BUILD)/serial.log || (echo "FAIL: cow isolation"; exit 1)
> @grep -q "\[kern\] cow: pid=.* copy ok" $(BUILD)/serial.log || (echo "FAIL: cow fault path"; exit 1)
> @grep -q "proc: clone tid=.* tgid=" $(BUILD)/serial.log || (echo "FAIL: clone"; exit 1)
> @grep -q "thread: shared ok" $(BUILD)/serial.log || (echo "FAIL: clone shared mm"; exit 1)
> @grep -q "sign: kill pid=.* sig=15" $(BUILD)/serial.log || (echo "FAIL: kill delivery"; exit 1)
> @grep -q "sign: killed ok" $(BUILD)/serial.log || (echo "FAIL: signal exit code"; exit 1)
> @if [ -n "$(HAVE_MKFS)" ]; then \
    grep -q "\[kern\] blk: virtio-mmio sectors=" $(BUILD)/serial.log || (echo "FAIL: blk"; exit 1); \
    grep -q "ext2: hello from ext2" $(BUILD)/serial.log || (echo "FAIL: ext2"; exit 1); \
  else echo "SKIP: no mke2fs (块设备自检跳过)"; fi
> @grep -q "\[kern\] vfs: hello from ramfs" $(BUILD)/serial.log || (echo "FAIL: vfs"; exit 1)
> @grep -q "\[kern\] rootfs:" $(BUILD)/serial.log || (echo "FAIL: rootfs"; exit 1)
> @grep -q "\[thr\] A 0" $(BUILD)/serial.log || (echo "FAIL: thread A"; exit 1)
> @grep -q "\[thr\] B 0" $(BUILD)/serial.log || (echo "FAIL: thread B"; exit 1)
> @grep -q "\[kern\] sched: done" $(BUILD)/serial.log || (echo "FAIL: sched done"; exit 1)
> @grep -q "BIG_KERNEL_OK" $(BUILD)/serial.log || (echo "FAIL: big kernel"; exit 1)
> @grep -q "ALL_OK" $(BUILD)/serial.log || (echo "FAIL: all"; exit 1)
> @echo "BARE_P0_OK"
> @$(MAKE) -s test-cmd

test-cmd: $(BUILD)/kernel.bin
> @sh tools/serial_cmd.sh "$(QEMU)" "$<" "$(BUILD)/cmd.log"
> @echo "BARE_CMD_OK"

# M4 验收：EL0 init -> shell -> ls / cat
test-m4: $(BUILD)/kernel.bin
> @sh tools/serial_m4.sh "$(QEMU)" "$<" "$(BUILD)/m4.log"
> @echo "BARE_M4_OK"

clean:
> rm -rf $(BUILD)
