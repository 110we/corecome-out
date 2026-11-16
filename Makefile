CROSS_COMPILE ?= aarch64-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
HOST_CC ?= cc

BUILD   := build
ARCH    := kernels/COXVBN/arch/aarch64
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
           -fno-stack-protector -fno-pic -mgeneral-regs-only -mno-outline-atomics
ASFLAGS := $(CFLAGS)
KERNEL_INCLUDES := -Ikernels/COXVBN/core -Ikernels/COXVBN/memory -Ikernels/COXVBN/platform \
                   -Ikernels/COXVBN/security -Ikernels/COXVBN/recovery \
                   -Ikernels/RedLab/policy -Ikernels/RedLab/profile
LDFLAGS := -nostdlib -Wl,-T,$(ARCH)/linker.ld -Wl,--build-id=none

SOURCES_C := kernels/COXVBN/core/main.c kernels/COXVBN/core/execution_context.c kernels/COXVBN/platform/uart.c kernels/COXVBN/platform/fdt.c kernels/COXVBN/security/key_vault.c \
             kernels/COXVBN/recovery/information_store.c kernels/COXVBN/recovery/emergency_recovery.c \
             kernels/COXVBN/memory/phys_alloc.c kernels/COXVBN/memory/mmu.c kernels/COXVBN/security/module_registry.c kernels/COXVBN/core/exception.c \
             kernels/COXVBN/core/exception_decode.c kernels/COXVBN/platform/gicv2.c kernels/COXVBN/platform/arm_timer.c kernels/RedLab/policy/redlab_policy.c \
             kernels/RedLab/profile/privacy_profile.c
SOURCES_S := $(ARCH)/boot.S
OBJECTS   := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES_C)) \
             $(patsubst %.S,$(BUILD)/%.o,$(SOURCES_S))

.PHONY: all clean run disassemble test-host
all: $(BUILD)/coxvbn.elf $(BUILD)/coxvbn.bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_INCLUDES) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/coxvbn.elf: $(OBJECTS) $(ARCH)/linker.ld
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

$(BUILD)/coxvbn.bin: $(BUILD)/coxvbn.elf
	$(OBJCOPY) -O binary $< $@

test-host:
	@mkdir -p $(BUILD)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror $(KERNEL_INCLUDES) tests/COXVBN/platform/fdt_test.c kernels/COXVBN/platform/fdt.c -o $(BUILD)/fdt_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror $(KERNEL_INCLUDES) tests/COXVBN/memory/phys_alloc_test.c kernels/COXVBN/memory/phys_alloc.c -o $(BUILD)/phys_alloc_test
	$(BUILD)/phys_alloc_test
	$(BUILD)/fdt_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror $(KERNEL_INCLUDES) tests/COXVBN/recovery/recovery_test.c kernels/COXVBN/security/key_vault.c kernels/COXVBN/recovery/information_store.c kernels/COXVBN/recovery/emergency_recovery.c -o $(BUILD)/recovery_test
	$(BUILD)/recovery_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror $(KERNEL_INCLUDES) tests/COXVBN/security/receiver_key_test.c kernels/COXVBN/security/key_vault.c -o $(BUILD)/receiver_key_test
	$(BUILD)/receiver_key_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -pthread $(KERNEL_INCLUDES) tests/COXVBN/security/module_registry_test.c kernels/COXVBN/security/module_registry.c kernels/COXVBN/security/key_vault.c -o $(BUILD)/module_registry_test
	$(BUILD)/module_registry_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror $(KERNEL_INCLUDES) tests/COXVBN/architecture/exception_frame_test.c -o $(BUILD)/exception_frame_test
	$(BUILD)/exception_frame_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror $(KERNEL_INCLUDES) tests/COXVBN/architecture/exception_decode_test.c kernels/COXVBN/core/exception_decode.c -o $(BUILD)/exception_decode_test
	$(BUILD)/exception_decode_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror $(KERNEL_INCLUDES) tests/COXVBN/architecture/exception_dispatch_test.c kernels/COXVBN/core/exception.c kernels/COXVBN/core/exception_decode.c kernels/COXVBN/platform/gicv2.c -o $(BUILD)/exception_dispatch_test
	$(BUILD)/exception_dispatch_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror $(KERNEL_INCLUDES) tests/COXVBN/platform/gicv2_test.c kernels/COXVBN/platform/gicv2.c -o $(BUILD)/gicv2_test
	$(BUILD)/gicv2_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror $(KERNEL_INCLUDES) tests/COXVBN/platform/arm_timer_test.c kernels/COXVBN/platform/arm_timer.c -o $(BUILD)/arm_timer_test
	$(BUILD)/arm_timer_test
	$(HOST_CC) -DCOXVBN_REDLAB_TESTING -std=c11 -O2 -Wall -Wextra -Werror $(KERNEL_INCLUDES) tests/RedLab/policy/redlab_policy_test.c kernels/RedLab/policy/redlab_policy.c kernels/COXVBN/core/execution_context.c kernels/COXVBN/security/module_registry.c kernels/COXVBN/security/key_vault.c -o $(BUILD)/redlab_policy_test
	$(BUILD)/redlab_policy_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror $(KERNEL_INCLUDES) tests/RedLab/profile/privacy_profile_test.c kernels/RedLab/profile/privacy_profile.c -o $(BUILD)/privacy_profile_test
	$(BUILD)/privacy_profile_test

run: all
	qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic -monitor none -serial stdio -kernel $(BUILD)/coxvbn.bin

disassemble: $(BUILD)/coxvbn.elf
	$(OBJDUMP) -d $<

clean:
	rm -rf $(BUILD)
