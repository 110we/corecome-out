#include "phys_alloc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){struct fdt_info f;struct phys_allocator_stats s;uintptr_t a,b;memset(&f,0,sizeof(f));f.memory_count=1;f.memory[0].base=0x40000000ULL;f.memory[0].size=0x01000000ULL;f.reserved_count=1;f.reserved[0].base=0x40002000ULL;f.reserved[0].size=0x2000ULL;assert(phys_allocator_init(&f,0x40008000U,0x40010000U,0x40010000U,0x40011000U)==0);phys_allocator_get_stats(&s);assert(s.total_pages==4096U&&s.free_pages>0);a=phys_alloc_page();b=phys_alloc_page();assert(a&&b&&a!=b);assert(!(a&4095U)&&!(b&4095U));assert(a<0x40002000U||a>=0x40004000U);assert(a<0x40008000U||a>=0x40011000U);assert(phys_free_page(a)==0);assert(phys_free_page(a)==-3);assert(phys_free_page(b+1U)==-1);assert(phys_free_page(b)==0);assert(phys_free_page(0)==-2);/* High physical ranges outside the 4-GiB tracking window must be ignored,
    not rounded with overflowing arithmetic into low-page allocations. */
 memset(&f,0,sizeof(f));f.memory_count=1;
 f.memory[0].base=UINT64_MAX-1023U;f.memory[0].size=512U;
 assert(phys_allocator_init(&f,0x40008000U,0x40010000U,
                           0x40010000U,0x40011000U)==-3);
 phys_allocator_get_stats(&s);assert(s.total_pages==0U&&s.free_pages==0U);
 puts("COXVBN physical allocator tests: PASS");return 0;}
