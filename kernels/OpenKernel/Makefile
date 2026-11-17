CROSS_COMPILE ?= aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
BUILD := build
ARCH := arch/aarch64
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -mgeneral-regs-only
LDFLAGS := -nostdlib -Wl,-T,$(ARCH)/linker.ld -Wl,--build-id=none

SOURCES_C := core/kernel.c platform/uart.c
SOURCES_S := $(ARCH)/boot.S
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES_C)) $(patsubst %.S,$(BUILD)/%.o,$(SOURCES_S))

.PHONY: all clean
all: $(BUILD)/openkernel.elf

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/openkernel.elf: $(OBJECTS) $(ARCH)/linker.ld
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

clean:
	rm -rf $(BUILD)
