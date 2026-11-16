/* driver_model.c — 固定容量、无动态分配的教学级设备/驱动注册表。
 * FDT 提供设备实例；compatible 精确匹配驱动；probe 成功才算绑定。
 */
#include "kern.h"
#define DEVICE_MAX DTB_DEVICE_MAX
#define DRIVER_MAX 8
static struct device devices[DEVICE_MAX];
static const struct driver *drivers[DRIVER_MAX];
static u32 ndevices, ndrivers;
static int initialized;
static int str_eq(const char *a,const char *b){
    if(!a||!b)return 0;
    while(*a&&*a==*b){a++;b++;}
    return *a==*b;
}
static void copy_string(char *d,u32 cap,const char *s){
    u32 i=0;if(!cap)return;
    if(s)while(i+1<cap&&s[i]){d[i]=s[i];i++;}
    d[i]='\0';
}
int device_model_init(const struct dtb_info *dt){
    u32 i;ndevices=0;ndrivers=0;initialized=0;
    for(i=0;i<DEVICE_MAX;i++){
        devices[i].name[0]='\0';devices[i].compatible[0]='\0';
        devices[i].base=0;devices[i].size=0;
        devices[i].driver=0;devices[i].driver_data=0;devices[i].bound=0;
    }
    for(i=0;i<DRIVER_MAX;i++)drivers[i]=0;
    if(!dt||!dt->ok||dt->device_count>DEVICE_MAX)return -1;
    for(i=0;i<dt->device_count;i++){
        copy_string(devices[i].name,sizeof(devices[i].name),dt->devices[i].name);
        copy_string(devices[i].compatible,sizeof(devices[i].compatible),dt->devices[i].compatible);
        devices[i].base=dt->devices[i].base;devices[i].size=dt->devices[i].size;ndevices++;
    }
    initialized=1;return 0;
}
int driver_register(const struct driver *drv){
    u32 i;
    if(!initialized||!drv||!drv->name||!drv->compatible||!drv->probe||ndrivers>=DRIVER_MAX)return -1;
    for(i=0;i<ndrivers;i++)if(drivers[i]==drv||str_eq(drivers[i]->name,drv->name))return -1;
    drivers[ndrivers++]=drv;return 0;
}
int driver_bind_all(void){
    u32 i,j;int bound=0;if(!initialized)return -1;
    for(i=0;i<ndevices;i++){
        if(devices[i].bound)continue;
        for(j=0;j<ndrivers;j++){
            const struct driver *drv=drivers[j];
            if(str_eq(devices[i].compatible,drv->compatible)&&drv->probe(&devices[i])==0){
                devices[i].driver=drv;devices[i].bound=1;bound++;break;
            }
        }
    }
    return bound;
}
u32 device_count(void){return initialized?ndevices:0;}
u32 device_bound_count(void){
    u32 i,n=0;if(!initialized)return 0;
    for(i=0;i<ndevices;i++)if(devices[i].bound)n++;
    return n;
}
const struct device *device_get(u32 i){return !initialized||i>=ndevices?0:&devices[i];}
