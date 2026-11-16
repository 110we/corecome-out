#include "mmu.h"

#include <stdint.h>

#define L1_ENTRIES 512U
#define L2_ENTRIES 512U
#define BLOCK_SIZE (2ULL * 1024ULL * 1024ULL)
#define RAM_WINDOW_BASE 0x40000000ULL
#define RAM_WINDOW_END  0x80000000ULL
#define DEVICE_GIC_BASE 0x08000000ULL
#define DEVICE_UART_BASE 0x09000000ULL

#define DESC_VALID       (1ULL << 0)
#define DESC_TABLE       (1ULL << 1)
#define DESC_AF          (1ULL << 10)
#define DESC_SH_INNER    (3ULL << 8)
#define DESC_ATTR_DEVICE (1ULL << 2)
#define DESC_PXN         (1ULL << 53)
#define DESC_UXN         (1ULL << 54)
#define DESC_BLOCK       DESC_VALID
#define DESC_TABLE_PAGE  (DESC_VALID | DESC_TABLE)

static uint64_t l1[ L1_ENTRIES ] __attribute__((aligned(4096)));
static uint64_t device_l2[L2_ENTRIES] __attribute__((aligned(4096)));
static uint64_t ram_l2[L2_ENTRIES] __attribute__((aligned(4096)));

static uint64_t normal_block(uint64_t address)
{
    /* Kernel text executes at EL1; keep privileged execution enabled
       for this bootstrap identity map, while denying EL0 execution. */
    return (address & ~(BLOCK_SIZE - 1ULL)) | DESC_BLOCK | DESC_AF |
           DESC_SH_INNER | DESC_UXN;
}

static uint64_t device_block(uint64_t address)
{
    return (address & ~(BLOCK_SIZE - 1ULL)) | DESC_BLOCK | DESC_AF |
           DESC_ATTR_DEVICE | DESC_PXN | DESC_UXN;
}

static int range_contains(const struct fdt_info *fdt,
                          uint64_t start, uint64_t end)
{
    uint32_t i;
    for (i = 0; i < fdt->memory_count; ++i) {
        uint64_t base = fdt->memory[i].base;
        uint64_t size = fdt->memory[i].size;
        if (base <= start && size <= UINT64_MAX - base &&
            base + size >= end) return 1;
    }
    return 0;
}

int mmu_enable(const struct fdt_info *fdt, uintptr_t kernel_start,
               uintptr_t kernel_end, uintptr_t boot_fdt)
{
    uint32_t i;
    uint64_t ram_end;
    if (fdt == 0 || fdt->memory_count == 0U ||
        fdt->memory_count > COXVBN_FDT_MAX_RANGES ||
        kernel_start >= kernel_end ||
        (kernel_start < RAM_WINDOW_BASE) || kernel_end > RAM_WINDOW_END ||
        (uint64_t)boot_fdt < RAM_WINDOW_BASE ||
        (uint64_t)boot_fdt >= RAM_WINDOW_END) return -1;

    /* The initial implementation deliberately supports one bounded,
       identity-mapped 1-GiB RAM window and QEMU virt's device apertures. */
    if (!range_contains(fdt, kernel_start, kernel_end) ||
        !range_contains(fdt, (uint64_t)boot_fdt,
                        (uint64_t)boot_fdt + fdt->total_size)) return -2;

    for (i = 0; i < L1_ENTRIES; ++i) {
        l1[i] = 0;
        device_l2[i] = 0;
        ram_l2[i] = 0;
    }

    /* L1[0] covers the low 1 GiB; expose only the known GIC and PL011
       2-MiB device windows as Device-nGnRnE, execute-never mappings. */
    l1[0] = ((uint64_t)(uintptr_t)device_l2 & ~0xfffULL) |
            DESC_TABLE_PAGE;
    device_l2[DEVICE_GIC_BASE / BLOCK_SIZE] =
        device_block(DEVICE_GIC_BASE);
    device_l2[DEVICE_UART_BASE / BLOCK_SIZE] =
        device_block(DEVICE_UART_BASE);

    /* L1[1] covers 0x40000000..0x80000000. Map only complete 2-MiB
       blocks reported as RAM by the FDT; do not map holes as normal RAM. */
    l1[1] = ((uint64_t)(uintptr_t)ram_l2 & ~0xfffULL) |
            DESC_TABLE_PAGE;
    for (i = 0; i < fdt->memory_count; ++i) {
        uint64_t base = fdt->memory[i].base;
        uint64_t size = fdt->memory[i].size;
        uint64_t end, block;
        if (size == 0U || base > UINT64_MAX - size) return -3;
        end = base + size;
        if (base < RAM_WINDOW_BASE || end > RAM_WINDOW_END) return -4;
        block = (base + BLOCK_SIZE - 1ULL) & ~(BLOCK_SIZE - 1ULL);
        for (; block <= end && end - block >= BLOCK_SIZE;
             block += BLOCK_SIZE) {
            uint64_t index = (block - RAM_WINDOW_BASE) / BLOCK_SIZE;
            if (index >= L2_ENTRIES) return -5;
            ram_l2[index] = normal_block(block);
        }
    }
    ram_end = (uint64_t)kernel_end;
    if (!range_contains(fdt, kernel_start, ram_end)) return -6;

    /* MAIR: Attr0 = Normal WB/WA, Attr1 = Device-nGnRnE.
       TCR: 39-bit VA, 4-KiB granule, inner-shareable WB/WA walks,
       40-bit physical address size. */
    {
        uint64_t mair = 0x00000000000000ffULL;
        uint64_t tcr = 25ULL | (1ULL << 8) | (1ULL << 10) |
                       (3ULL << 12) | (25ULL << 16) | (1ULL << 23) |
                       (2ULL << 32);
        uint64_t ttbr = (uint64_t)(uintptr_t)l1;
        uint64_t sctlr;
        __asm__ volatile("dsb ishst" ::: "memory");
        __asm__ volatile("msr mair_el1, %0" :: "r"(mair) : "memory");
        __asm__ volatile("msr tcr_el1, %0" :: "r"(tcr) : "memory");
        __asm__ volatile("msr ttbr0_el1, %0" :: "r"(ttbr) : "memory");
        __asm__ volatile("isb" ::: "memory");
        __asm__ volatile("tlbi vmalle1" ::: "memory");
        __asm__ volatile("dsb ish" ::: "memory");
        __asm__ volatile("isb" ::: "memory");
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
        sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
        __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr) : "memory");
        __asm__ volatile("isb" ::: "memory");
    }
    return 0;
}
