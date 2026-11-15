#include "phys_alloc.h"
#define BITMAP_BYTES (COXVBN_PHYS_MAX_PAGES/8U)
static uint8_t bitmap[BITMAP_BYTES];
static uint32_t limit_pages,managed_pages,free_pages;
static int ready;
static void unavailable(uint32_t p){uint8_t m=(uint8_t)(1U<<(p&7U));if(!(bitmap[p>>3]&m)){bitmap[p>>3]|=m;if(free_pages)--free_pages;}}
static void available(uint32_t p){uint8_t m=(uint8_t)(1U<<(p&7U));if(bitmap[p>>3]&m){bitmap[p>>3]&=(uint8_t)~m;++free_pages;}}
static uint64_t up(uint64_t v){return (v+4095U)&~(uint64_t)4095U;}
static void reserve(uint64_t b,uint64_t s){uint64_t e,f,l,p;if(!s||b>UINT64_MAX-s)return;e=b+s;f=b/4096U;l=e/4096U;if(e&4095U)++l;if(l>limit_pages)l=limit_pages;if(f>l)return;for(p=f;p<l;++p)unavailable((uint32_t)p);}
int phys_allocator_init(const struct fdt_info *f,uintptr_t ks,uintptr_t ke,uintptr_t fs,uintptr_t fe){
 uint32_t i;if(!f||!f->memory_count||f->memory_count>COXVBN_FDT_MAX_RANGES||f->reserved_count>COXVBN_FDT_MAX_RANGES||ks>=ke||fs>=fe)return -1;
 for (i = 0; i < BITMAP_BYTES; ++i) {
  bitmap[i] = 0xffU;
 }
 limit_pages = 0U;
 managed_pages = 0U;
 free_pages = 0U;
 ready = 0;
 for(i=0;i<f->memory_count;++i){uint64_t b=f->memory[i].base,s=f->memory[i].size,e,first,last,p;if(!s||b>UINT64_MAX-s)return -2;e=b+s;first=up(b)/4096U;last=e/4096U;if(first>=COXVBN_PHYS_MAX_PAGES)continue;if(last>COXVBN_PHYS_MAX_PAGES)last=COXVBN_PHYS_MAX_PAGES;if(first>last)continue;if(last>limit_pages)limit_pages=(uint32_t)last;for(p=first;p<last;++p)available((uint32_t)p);}
 if (!limit_pages || !free_pages) return -3;
 managed_pages = free_pages;
 for(i=0;i<f->reserved_count;++i)reserve(f->reserved[i].base,f->reserved[i].size);
 reserve(ks,(uint64_t)(ke-ks));reserve(fs,(uint64_t)(fe-fs));unavailable(0);ready=1;return 0;
}
uintptr_t phys_alloc_page(void){uint32_t p;if(!ready||!free_pages)return 0;for(p=1;p<limit_pages;++p)if(!(bitmap[p>>3]&(uint8_t)(1U<<(p&7U)))){unavailable(p);return (uintptr_t)((uint64_t)p*4096U);}return 0;}
int phys_free_page(uintptr_t a){uint64_t p64;uint32_t p;if(!ready||(a&4095U))return -1;p64=(uint64_t)a/4096U;if(!p64||p64>=limit_pages)return -2;p=(uint32_t)p64;if(!(bitmap[p>>3]&(uint8_t)(1U<<(p&7U))))return -3;available(p);return 0;}
void phys_allocator_get_stats(struct phys_allocator_stats *s){if(s){s->total_pages=managed_pages;s->free_pages=ready?free_pages:0;}}
