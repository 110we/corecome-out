CROSS_COMPILE ?= aarch64-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump

BUILD   := build
ARCH    := arch/aarch64
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
           -fno-stack-protector -fno-pic -mgeneral-regs-only
ASFLAGS := $(CFLAGS)
LDFLAGS := -nostdlib -Wl,-T,$(ARCH)/linker.ld -Wl,--build-id=none

SOURCES_C := kernel/main.c kernel/uart.c kernel/fdt.c
SOURCES_S := $(ARCH)/boot.S
OBJECTS   := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES_C)) \
             $(patsubst %.S,$(BUILD)/%.o,$(SOURCES_S))

.PHONY: all clean run disassemble
all: $(BUILD)/coxvbn.elf $(BUILD)/coxvbn.bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/coxvbn.elf: $(OBJECTS) $(ARCH)/linker.ld
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

$(BUILD)/coxvbn.bin: $(BUILD)/coxvbn.elf
	$(OBJCOPY) -O binary $< $@

run: all
	qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic -monitor none -serial stdio -kernel $(BUILD)/coxvbn.bin

disassemble: $(BUILD)/coxvbn.elf
	$(OBJDUMP) -d $<

clean:
	rm -rf $(BUILD)
