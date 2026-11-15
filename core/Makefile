CROSS_COMPILE ?= aarch64-none-elf-
CC      := $(CROSS_COMPILE)gcc
QEMU    ?= qemu-system-aarch64

CFLAGS  := -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -mgeneral-regs-only -Wall -Wextra -Werror -O2 -mcpu=cortex-a53 -Iinclude
ASFLAGS := -ffreestanding -mcpu=cortex-a53 -Iinclude
LDFLAGS := -nostdlib -static -T linker.ld -Wl,--build-id=none

C_SRCS := src/kernel.c src/uart.c src/security.c src/security_plugin.c src/exception.c
S_SRCS := arch/aarch64/boot.S arch/aarch64/vectors.S
OBJS   := $(C_SRCS:.c=.o) $(S_SRCS:.S=.o)

.PHONY: all clean run
all: build/corecome.elf

build:
	mkdir -p $@

build/corecome.elf: $(OBJS) linker.ld | build
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(ASFLAGS) -c $< -o $@

run: build/corecome.elf
	$(QEMU) -machine virt -cpu cortex-a53 -m 512M -nographic -kernel $<

clean:
	rm -f $(OBJS)
	rm -rf build