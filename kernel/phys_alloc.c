#include "phys_alloc.h"

#define BITMAP_BYTES (COXVBN_PHYS_MAX_PAGES / 8U)

static uint8_t bitmap[BITMAP_BYTES];
/* A page is free only when bitmap says available; this second bitmap records
 * pages actually handed to callers so reserved pages can never be freed. */
static uint8_t allocated_bitmap[BITMAP_BYTES];
static uint32_t limit_pages;
static uint32_t managed_pages;
static uint32_t free_pages;
static int ready;

static uint8_t page_mask(uint32_t page)
{
    return (uint8_t)(1U << (page & 7U));
}

static int bit_is_set(const uint8_t *map, uint32_t page)
{
    return (map[page >> 3] & page_mask(page)) != 0U;
}

static void bit_set(uint8_t *map, uint32_t page)
{
    map[page >> 3] |= page_mask(page);
}

static void bit_clear(uint8_t *map, uint32_t page)
{
    map[page >> 3] &= (uint8_t)~page_mask(page);
}

static void unavailable(uint32_t page)
{
    if (!bit_is_set(bitmap, page)) {
        bit_set(bitmap, page);
        if (free_pages != 0U) {
            --free_pages;
        }
    }
}

static void available(uint32_t page)
{
    if (bit_is_set(bitmap, page)) {
        bit_clear(bitmap, page);
        ++free_pages;
    }
}

static int align_up_page(uint64_t value, uint64_t *aligned)
{
    if (aligned == 0 || value > UINT64_MAX - (COXVBN_PAGE_SIZE - 1U)) {
        return -1;
    }
    *aligned = (value + (COXVBN_PAGE_SIZE - 1U)) &
               ~((uint64_t)COXVBN_PAGE_SIZE - 1U);
    return 0;
}

static void reserve(uint64_t base, uint64_t size)
{
    uint64_t end;
    uint64_t first;
    uint64_t last;
    uint64_t page;

    if (size == 0U || base > UINT64_MAX - size) {
        return;
    }

    end = base + size;
    first = base / COXVBN_PAGE_SIZE;
    last = end / COXVBN_PAGE_SIZE;
    if ((end & (COXVBN_PAGE_SIZE - 1U)) != 0U) {
        ++last;
    }
    if (last > limit_pages) {
        last = limit_pages;
    }
    if (first >= last) {
        return;
    }

    for (page = first; page < last; ++page) {
        unavailable((uint32_t)page);
    }
}

int phys_allocator_init(const struct fdt_info *fdt,
                        uintptr_t kernel_start, uintptr_t kernel_end,
                        uintptr_t fdt_start, uintptr_t fdt_end)
{
    uint32_t i;

    if (fdt == 0 || fdt->memory_count == 0U ||
        fdt->memory_count > COXVBN_FDT_MAX_RANGES ||
        fdt->reserved_count > COXVBN_FDT_MAX_RANGES ||
        kernel_start >= kernel_end || fdt_start >= fdt_end) {
        return -1;
    }

    for (i = 0; i < BITMAP_BYTES; ++i) {
        bitmap[i] = 0xffU;
        allocated_bitmap[i] = 0U;
    }

    limit_pages = 0U;
    managed_pages = 0U;
    free_pages = 0U;
    ready = 0;

    for (i = 0; i < fdt->memory_count; ++i) {
        uint64_t base = fdt->memory[i].base;
        uint64_t size = fdt->memory[i].size;
        uint64_t end;
        uint64_t aligned_base;
        uint64_t first;
        uint64_t last;
        uint64_t page;

        if (size == 0U || base > UINT64_MAX - size ||
            align_up_page(base, &aligned_base) != 0) {
            return -2;
        }

        end = base + size;
        first = aligned_base / COXVBN_PAGE_SIZE;
        last = end / COXVBN_PAGE_SIZE;
        if (first >= COXVBN_PHYS_MAX_PAGES) {
            continue;
        }
        if (last > COXVBN_PHYS_MAX_PAGES) {
            last = COXVBN_PHYS_MAX_PAGES;
        }
        if (first >= last) {
            continue;
        }
        if (last > limit_pages) {
            limit_pages = (uint32_t)last;
        }

        for (page = first; page < last; ++page) {
            available((uint32_t)page);
        }
    }

    if (limit_pages == 0U || free_pages == 0U) {
        return -3;
    }

    managed_pages = free_pages;

    for (i = 0; i < fdt->reserved_count; ++i) {
        reserve(fdt->reserved[i].base, fdt->reserved[i].size);
    }

    reserve((uint64_t)kernel_start,
            (uint64_t)(kernel_end - kernel_start));
    reserve((uint64_t)fdt_start, (uint64_t)(fdt_end - fdt_start));
    unavailable(0U);
    ready = 1;
    return 0;
}

uintptr_t phys_alloc_page(void)
{
    uint32_t page;

    if (!ready || free_pages == 0U) {
        return 0U;
    }

    for (page = 1U; page < limit_pages; ++page) {
        if (!bit_is_set(bitmap, page)) {
            unavailable(page);
            bit_set(allocated_bitmap, page);
            return (uintptr_t)((uint64_t)page * COXVBN_PAGE_SIZE);
        }
    }
    return 0U;
}

int phys_free_page(uintptr_t address)
{
    uint64_t page64;
    uint32_t page;

    if (!ready || (address & (COXVBN_PAGE_SIZE - 1U)) != 0U) {
        return -1;
    }

    page64 = (uint64_t)address / COXVBN_PAGE_SIZE;
    if (page64 == 0U || page64 >= limit_pages) {
        return -2;
    }

    page = (uint32_t)page64;
    if (!bit_is_set(allocated_bitmap, page)) {
        return -3;
    }

    bit_clear(allocated_bitmap, page);
    available(page);
    return 0;
}

void phys_allocator_get_stats(struct phys_allocator_stats *stats)
{
    if (stats != 0) {
        stats->total_pages = managed_pages;
        stats->free_pages = ready ? free_pages : 0U;
    }
}
