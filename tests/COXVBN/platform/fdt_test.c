#include "fdt.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define H 40U
#define BEGIN_NODE 1U
#define END_NODE 2U
#define PROP 3U
#define END 9U
static _Alignas(8) uint8_t blob[512];
static void put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static uint32_t get32(const uint8_t*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint32_t w(uint32_t p,uint32_t v){put32(blob+p,v);return p+4;}
static uint32_t str(uint32_t p,const char*s){size_t n=strlen(s)+1;memcpy(blob+p,s,n);p+=(uint32_t)n;while(p&3U)blob[p++]=0;return p;}
static uint32_t prop(uint32_t p,uint32_t n,uint32_t no,const uint8_t*v){p=w(p,PROP);p=w(p,n);p=w(p,no);memcpy(blob+p,v,n);p+=n;while(p&3U)blob[p++]=0;return p;}
static void build(void){
 uint32_t p=H,so,sto,ss,sts;
 const uint8_t ac[4]={0,0,0,2},sc[4]={0,0,0,2};
 const uint8_t dt[7]={'m','e','m','o','r','y',0};
 const uint8_t reg[16]={0,0,0,0,0x40,0,0,0,0,0,0,0,0,0,0x10,0};
 memset(blob,0,sizeof(blob));put32(blob+p,0);put32(blob+p+4,0x1000);put32(blob+p+8,0);put32(blob+p+12,0x1000);p+=32;
 so=p;p=w(p,BEGIN_NODE);p=str(p,"");p=prop(p,4,0,ac);p=prop(p,4,16,sc);
 p=w(p,BEGIN_NODE);p=str(p,"memory@40000000");p=prop(p,7,28,dt);p=prop(p,16,40,reg);
 p=w(p,END_NODE);
 p=w(p,BEGIN_NODE);p=str(p,"reserved-memory");
 p=prop(p,4,0,(const uint8_t[]){0,0,0,1});
 p=prop(p,4,16,(const uint8_t[]){0,0,0,1});
 p=w(p,BEGIN_NODE);p=str(p,"secure@200000");
 p=prop(p,8,40,(const uint8_t[]){0,0x20,0,0,0,0,0x10,0});
 p=w(p,BEGIN_NODE);p=str(p,"nested@300000");
 p=prop(p,8,40,(const uint8_t[]){0,0x30,0,0,0,0,0x10,0});
 p=w(p,END_NODE);p=w(p,END_NODE);p=w(p,END_NODE);
 p=w(p,BEGIN_NODE);p=str(p,"interrupt-controller@8000000");
 p=prop(p,19,44,(const uint8_t[]){'a','r','m',',','c','o','r','t','e','x','-','a','1','5','-','g','i','c',0});
 p=prop(p,32,40,(const uint8_t[]){
  0,0,0,0,8,0,0,0, 0,0,0,0,0,1,0,0,
  0,0,0,0,8,1,0,0, 0,0,0,0,0,1,0,0
 });
 p=w(p,END_NODE);
 p=w(p,END_NODE);p=w(p,END);ss=p-so;
 sto=p;p=str(p,"#address-cells");p=str(p,"#size-cells");p=str(p,"device_type");p=str(p,"reg");p=str(p,"compatible");sts=p-sto;
 put32(blob,0xd00dfeedU);put32(blob+4,p);put32(blob+8,so);put32(blob+12,sto);put32(blob+16,H);put32(blob+20,17);put32(blob+24,16);put32(blob+28,0);put32(blob+32,sts);put32(blob+36,ss);
}
static void valid(void){struct fdt_info i;build();assert(fdt_parse(blob,&i)==0);assert(i.memory_count==1);assert(i.memory[0].base==0x40000000ULL);assert(i.memory[0].size==0x1000ULL);assert(i.reserved_count==2);assert(i.reserved[0].base==0x1000ULL);assert(i.reserved[1].base==0x200000ULL);assert(i.reserved[1].size==0x1000ULL);assert(i.gicv2_found==1U);assert(i.gicv2_distributor.base==0x08000000ULL);assert(i.gicv2_distributor.size==0x10000ULL);assert(i.gicv2_cpu_interface.base==0x08010000ULL);assert(i.gicv2_cpu_interface.size==0x10000ULL);}
static void bad_magic(void){struct fdt_info i;build();put32(blob,0);assert(fdt_parse(blob,&i)==-3);}
static void overlap(void){struct fdt_info i;build();put32(blob+12,get32(blob+8));assert(fdt_inspect(blob,&i)==-6);}
static void bad_token(void){struct fdt_info i;build();put32(blob+get32(blob+8),0xdeadbeefU);assert(fdt_parse(blob,&i)==-19);}
static void no_reservation_terminator(void){struct fdt_info i;build();put32(blob+8,H+16U);put32(blob+12,H+16U+get32(blob+36));assert(fdt_parse(blob,&i)!=0);}
int main(void){valid();bad_magic();overlap();bad_token();no_reservation_terminator();puts("COXVBN host FDT tests: PASS");return 0;}
