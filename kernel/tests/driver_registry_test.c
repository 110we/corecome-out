#include "kern.h"
static int vp, up, gp;
static int probe_v(struct device *d){vp++;return d->base==0x0a000000UL&&d->size>=0x100?0:-1;}
static int probe_u(struct device *d){up++;return d->base==0x09000000UL?0:-1;}
static int probe_g(struct device *d){gp++;return d->base==0x08000000UL&&d->base2==0x08010000UL?0:-1;}
static const struct driver vd={"test-virtio","virtio,mmio",probe_v};
static const struct driver ud={"test-uart","arm,pl011",probe_u};
static const struct driver gd={"test-gic","arm,cortex-a15-gic",probe_g};
static void setstr(char *d,const char *s){int i=0;do{d[i]=s[i];}while(s[i++]!='\0');}
int main(void){
    struct dtb_info dt={0};const struct device *d;
    dt.ok=1;dt.device_count=4;
    setstr(dt.devices[0].name,"virtio_mmio@a000000");setstr(dt.devices[0].compatible,"virtio,mmio");
    dt.devices[0].base=0x0a000000UL;dt.devices[0].size=0x200;
    setstr(dt.devices[1].name,"pl011@9000000");setstr(dt.devices[1].compatible,"arm,pl011");
    dt.devices[1].base=0x09000000UL;dt.devices[1].size=0x1000;
    setstr(dt.devices[2].name,"virtio_mmio@a000200");setstr(dt.devices[2].compatible,"virtio,mmio");
    dt.devices[2].base=0x0a000200UL;dt.devices[2].size=0x200;
    setstr(dt.devices[3].name,"intc@8000000");setstr(dt.devices[3].compatible,"arm,cortex-a15-gic");
    dt.devices[3].base=0x08000000UL;dt.devices[3].size=0x10000;
    dt.devices[3].base2=0x08010000UL;dt.devices[3].size2=0x10000;
    if(device_model_init(&dt)||device_count()!=4)return 1;
    if(driver_register(&vd)||driver_register(&ud)||driver_register(&gd))return 2;
    if(driver_bind_all()!=3||vp!=2||up!=1||gp!=1||device_bound_count()!=3)return 3;
    d=device_get(0);if(!d||!d->bound||d->driver!=&vd)return 4;
    d=device_get(1);if(!d||!d->bound||d->driver!=&ud)return 5;
    d=device_get(2);if(!d||d->bound)return 6;
    d=device_get(3);if(!d||!d->bound||d->base2!=0x08010000UL)return 7;
    if(device_get(4)||driver_register(&vd)==0)return 8;
    if(device_model_init(0)==0||device_count()!=0||driver_bind_all()!=-1)return 9;
    return 0;
}
