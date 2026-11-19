#include "mmu.h"

#include <stdint.h>

#define L1_ENTRIES 512U
#define L2_ENTRIES 512U
#define L3_ENTRIES 512U
#define BLOCK_SIZE (2ULL * 1024ULL * 1024ULL)
#define PAGE_SIZE 4096ULL
#define RAM_WINDOW_BASE 0x40000000ULL
#define RAM_WINDOW_END  0x80000000ULL
#define DEVICE_GIC_BASE 0x08000000ULL
#define DEVICE_UART_BASE 0x09000000ULL

#define DESC_VALID       (1ULL << 0)
#define DESC_TABLE       (1ULL << 1)
#define DESC_AF          (1ULL << 10)
#define DESC_SH_INNER    (3ULL << 8)
#define DESC_ATTR_DEVICE (1ULL << 2)
#define DESC_AP_PRIV_RO  (1ULL << 7)
#define DESC_PXN         (1ULL << 53)
#define DESC_UXN         (1ULL << 54)
#define DESC_BLOCK       DESC_VALID
#define DESC_TABLE_PAGE  (DESC_VALID | DESC_TABLE)
#define DESC_PAGE        (DESC_VALID | DESC_TABLE)

static uint64_t l1[L1_ENTRIES] __attribute__((aligned(4096)));
static uint64_t device_l2[L2_ENTRIES] __attribute__((aligned(4096)));
static uint64_t ram_l2[L2_ENTRIES] __attribute__((aligned(4096)));
static uint64_t kernel_l3[L3_ENTRIES] __attribute__((aligned(4096)));

extern char __kernel_text_start[];
extern char __kernel_text_end[];
extern char __kernel_rodata_start[];
extern char __kernel_rodata_end[];
extern char __kernel_data_start[];
extern char __kernel_data_end[];

static uint64_t normal_block(uint64_t address)
{
    /* Non-kernel RAM is writable but never executable at either EL. */
    return (address & ~(BLOCK_SIZE - 1ULL)) | DESC_BLOCK | DESC_AF |
           DESC_SH_INNER | DESC_PXN | DESC_UXN;
}

static uint64_t normal_page(uint64_t address)
{
    return (address & ~(PAGE_SIZE - 1ULL)) | DESC_PAGE | DESC_AF |
           DESC_SH_INNER | DESC_PXN | DESC_UXN;
}

static uint64_t readonly_executable_page(uint64_t address)
{
    /* EL1 read-only, EL1 executable, EL0 inaccessible/non-executable. */
    return (address & ~(PAGE_SIZE - 1ULL)) | DESC_PAGE | DESC_AF |
           DESC_SH_INNER | DESC_AP_PRIV_RO | DESC_UXN;
}

static uint64_t readonly_nx_page(uint64_t address)
{
    return (address & ~(PAGE_SIZE - 1ULL)) | DESC_PAGE | DESC_AF |
           DESC_SH_INNER | DESC_AP_PRIV_RO | DESC_PXN | DESC_UXN;
}

static int map_section(uint64_t *table, uint64_t block_base,
                       uint64_t start, uint64_t end, uint64_t flags)
{
    uint64_t address;
    if (start > end || (start & (PAGE_SIZE - 1ULL)) != 0U ||
        (end & (PAGE_SIZE - 1ULL)) != 0U ||
        start < block_base || end > block_base + BLOCK_SIZE)
        return -1;
    for (address = start; address < end; address += PAGE_SIZE) {
        uint64_t index = (address - block_base) / PAGE_SIZE;
        table[index] = flags == 0U ? normal_page(address) :
                       flags == 1U ? readonly_executable_page(address) :
                                     readonly_nx_page(address);
    }
    return 0;
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
    uint64_t kernel_block, kernel_block_index;
    uint64_t text_start = (uint64_t)(uintptr_t)__kernel_text_start;
    uint64_t text_end = (uint64_t)(uintptr_t)__kernel_text_end;
    uint64_t rodata_start = (uint64_t)(uintptr_t)__kernel_rodata_start;
    uint64_t rodata_end = (uint64_t)(uintptr_t)__kernel_rodata_end;
    uint64_t data_start = (uint64_t)(uintptr_t)__kernel_data_start;
    uint64_t data_end = (uint64_t)(uintptr_t)__kernel_data_end;

    if (fdt == 0 || fdt->memory_count == 0U ||
        fdt->memory_count > COXVBN_FDT_MAX_RANGES ||
        kernel_start >= kernel_end ||
        kernel_start < RAM_WINDOW_BASE || kernel_end > RAM_WINDOW_END ||
        (uint64_t)boot_fdt < RAM_WINDOW_BASE ||
        (uint64_t)boot_fdt >= RAM_WINDOW_END ||
        fdt->total_size == 0U ||
        (uint64_t)boot_fdt + fdt->total_size > RAM_WINDOW_END)
        return -1;

    if (!range_contains(fdt, kernel_start, kernel_end) ||
        !range_contains(fdt, (uint64_t)boot_fdt,
                        (uint64_t)boot_fdt + fdt->total_size))
        return -2;

    /* The first W^X implementation deliberately requires the complete
       kernel image to fit in one 2-MiB RAM block. Fail closed otherwise. */
    kernel_block = (uint64_t)kernel_start & ~(BLOCK_SIZE - 1ULL);
    if ((((uint64_t)kernel_end - 1ULL) & ~(BLOCK_SIZE - 1ULL)) !=
        kernel_block)
        return -3;
    if ((text_start & (PAGE_SIZE - 1ULL)) != 0U ||
        (text_end & (PAGE_SIZE - 1ULL)) != 0U ||
        (rodata_start & (PAGE_SIZE - 1ULL)) != 0U ||
        (rodata_end & (PAGE_SIZE - 1ULL)) != 0U ||
        (data_start & (PAGE_SIZE - 1ULL)) != 0U ||
        (data_end & (PAGE_SIZE - 1ULL)) != 0U ||
        text_start < kernel_start || text_end > kernel_end ||
        rodata_start < text_end || rodata_end > kernel_end ||
        data_start < rodata_end || data_end > kernel_end)
        return -4;

    for (i = 0; i < L1_ENTRIES; ++i) {
        l1[i] = 0;
        device_l2[i] = 0;
        ram_l2[i] = 0;
        kernel_l3[i] = 0;
    }

    /* QEMU virt low 1-GiB device aperture. */
    l1[0] = ((uint64_t)(uintptr_t)device_l2 & ~0xfffULL) |
            DESC_TABLE_PAGE;
    device_l2[DEVICE_GIC_BASE / BLOCK_SIZE] =
        (DEVICE_GIC_BASE & ~(BLOCK_SIZE - 1ULL)) | DESC_BLOCK |
        DESC_AF | DESC_ATTR_DEVICE | DESC_PXN | DESC_UXN;
    device_l2[DEVICE_UART_BASE / BLOCK_SIZE] =
        (DEVICE_UART_BASE & ~(BLOCK_SIZE - 1ULL)) | DESC_BLOCK |
        DESC_AF | DESC_ATTR_DEVICE | DESC_PXN | DESC_UXN;

    /* RAM uses L2 blocks except the kernel's block, which is split into
       4-KiB pages so text/rodata/data receive distinct access permissions. */
    l1[1] = ((uint64_t)(uintptr_t)ram_l2 & ~0xfffULL) |
            DESC_TABLE_PAGE;
    for (i = 0; i < fdt->memory_count; ++i) {
        uint64_t base = fdt->memory[i].base;
        uint64_t size = fdt->memory[i].size;
        uint64_t end, block;
        if (size == 0U || base > UINT64_MAX - size) return -5;
        end = base + size;
        if (base < RAM_WINDOW_BASE || end > RAM_WINDOW_END) return -6;
        block = (base + BLOCK_SIZE - 1ULL) & ~(BLOCK_SIZE - 1ULL);
        for (; block <= end && end - block >= BLOCK_SIZE;
             block += BLOCK_SIZE) {
            uint64_t index = (block - RAM_WINDOW_BASE) / BLOCK_SIZE;
            if (index >= L2_ENTRIES) return -7;
            ram_l2[index] = normal_block(block);
        }
    }

    kernel_block_index = (kernel_block - RAM_WINDOW_BASE) / BLOCK_SIZE;
    if (kernel_block_index >= L2_ENTRIES) return -8;
    for (i = 0; i < L3_ENTRIES; ++i)
        kernel_l3[i] = normal_page(kernel_block + (uint64_t)i * PAGE_SIZE);
    ram_l2[kernel_block_index] =
        ((uint64_t)(uintptr_t)kernel_l3 & ~0xfffULL) | DESC_TABLE_PAGE;

    if (map_section(kernel_l3, kernel_block, text_start, text_end, 1U) != 0 ||
        map_section(kernel_l3, kernel_block, rodata_start, rodata_end, 2U) != 0 ||
        map_section(kernel_l3, kernel_block, data_start, data_end, 0U) != 0)
        return -9;

    if (!coxvbn_mmu_range_mapped(ram_l2, L2_ENTRIES,
                                (uint64_t)kernel_start,
                                (uint64_t)kernel_end) ||
        !coxvbn_mmu_range_mapped(ram_l2, L2_ENTRIES,
                                (uint64_t)boot_fdt,
                                (uint64_t)boot_fdt + fdt->total_size))
        return -10;

    /* MAIR Attr0 = Normal WB/WA; Attr1 = Device-nGnRnE.
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
