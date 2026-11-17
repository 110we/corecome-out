CROSS_COMPILE ?= aarch64-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
HOST_CC ?= cc

BUILD   := build
ARCH    := arch/aarch64
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
           -fno-stack-protector -fno-pic -mgeneral-regs-only -mno-outline-atomics
ASFLAGS := $(CFLAGS)
LDFLAGS := -nostdlib -Wl,-T,$(ARCH)/linker.ld -Wl,--build-id=none

SOURCES_C := kernel/main.c kernel/execution_context.c kernel/uart.c kernel/fdt.c kernel/key_vault.c \
             kernel/information_store.c kernel/emergency_recovery.c \
             kernel/phys_alloc.c kernel/vm_page.c kernel/mmu.c kernel/mmu_layout.c kernel/module_registry.c kernel/exception.c \
             kernel/exception_decode.c kernel/gicv2.c kernel/arm_timer.c kernel/redlab_policy.c \
             kernel/privacy_profile.c kernel/wifi.c kernel/vfs.c kernel/device.c
SOURCES_S := $(ARCH)/boot.S
OBJECTS   := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES_C)) \
             $(patsubst %.S,$(BUILD)/%.o,$(SOURCES_S))

.PHONY: all clean run disassemble test-host
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

test-host:
	@mkdir -p $(BUILD)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/fdt_test.c kernel/fdt.c -o $(BUILD)/fdt_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/phys_alloc_test.c kernel/phys_alloc.c -o $(BUILD)/phys_alloc_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/vm_page_test.c kernel/vm_page.c -o $(BUILD)/vm_page_test
	$(BUILD)/vm_page_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/mmu_layout_test.c kernel/mmu_layout.c -o $(BUILD)/mmu_layout_test
	$(BUILD)/mmu_layout_test
	$(BUILD)/phys_alloc_test
	$(BUILD)/fdt_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/recovery_test.c kernel/key_vault.c kernel/information_store.c kernel/emergency_recovery.c -o $(BUILD)/recovery_test
	$(BUILD)/recovery_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/receiver_key_test.c kernel/key_vault.c -o $(BUILD)/receiver_key_test
	$(BUILD)/receiver_key_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -pthread -I kernel tests/module_registry_test.c kernel/module_registry.c kernel/key_vault.c -o $(BUILD)/module_registry_test
	$(BUILD)/module_registry_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/exception_frame_test.c -o $(BUILD)/exception_frame_test
	$(BUILD)/exception_frame_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/exception_decode_test.c kernel/exception_decode.c -o $(BUILD)/exception_decode_test
	$(BUILD)/exception_decode_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/exception_dispatch_test.c kernel/exception.c kernel/exception_decode.c kernel/gicv2.c -o $(BUILD)/exception_dispatch_test
	$(BUILD)/exception_dispatch_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/gicv2_test.c kernel/gicv2.c -o $(BUILD)/gicv2_test
	$(BUILD)/gicv2_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/arm_timer_test.c kernel/arm_timer.c -o $(BUILD)/arm_timer_test
	$(BUILD)/arm_timer_test
	$(HOST_CC) -DCOXVBN_REDLAB_TESTING -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/redlab_policy_test.c kernel/redlab_policy.c kernel/execution_context.c kernel/module_registry.c kernel/key_vault.c -o $(BUILD)/redlab_policy_test
	$(BUILD)/redlab_policy_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/redlab_admission_test.c kernel/redlab_policy.c kernel/execution_context.c kernel/module_registry.c kernel/key_vault.c -o $(BUILD)/redlab_admission_test
	$(BUILD)/redlab_admission_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/privacy_profile_test.c kernel/privacy_profile.c -o $(BUILD)/privacy_profile_test
	$(BUILD)/privacy_profile_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/wifi_test.c kernel/wifi.c -o $(BUILD)/wifi_test
	$(BUILD)/wifi_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/vfs_test.c kernel/vfs.c -o $(BUILD)/vfs_test
	$(BUILD)/vfs_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I kernel tests/device_test.c kernel/device.c -o $(BUILD)/device_test
	$(BUILD)/device_test

run: all
	qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic -monitor none -serial stdio -kernel $(BUILD)/coxvbn.bin

disassemble: $(BUILD)/coxvbn.elf
	$(OBJDUMP) -d $<

clean:
	rm -rf $(BUILD)
