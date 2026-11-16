#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "redlab_policy.h"
#include "module_registry.h"
struct audit_log { size_t count; enum coxvbn_redlab_decision last; };
static void record(const struct coxvbn_redlab_audit_event *e,void *ctx){struct audit_log *l=ctx;assert(e&&e->version==COXVBN_REDLAB_AUDIT_VERSION);l->count++;l->last=e->decision;}
static struct coxvbn_redlab_rule rule4(uint8_t a,uint8_t b,uint8_t c,uint8_t d,uint8_t pre,uint16_t lo,uint16_t hi,uint64_t from,uint64_t to,bool ex){
 struct coxvbn_redlab_rule r;memset(&r,0,sizeof(r));r.target.family=COXVBN_IP_V4;r.target.prefix=pre;r.target.network[0]=a;r.target.network[1]=b;r.target.network[2]=c;r.target.network[3]=d;r.first_port=lo;r.last_port=hi;r.not_before=from;r.expires_at=to;r.exclude=ex;return r;}
static bool recovery_verify(const uint8_t *proof,size_t len,void *ctx){(void)ctx;return proof&&len==1U&&proof[0]==0x5aU;}
static bool verify_key(const uint8_t *key,size_t kl,const uint8_t *proof,size_t pl,void *ctx){(void)ctx;return key&&proof&&kl==1U&&pl==1U&&key[0]==proof[0];}
static struct coxvbn_ip_addr ip4(uint8_t a,uint8_t b,uint8_t c,uint8_t d){struct coxvbn_ip_addr x;memset(&x,0,sizeof(x));x.family=COXVBN_IP_V4;x.bytes[0]=a;x.bytes[1]=b;x.bytes[2]=c;x.bytes[3]=d;return x;}
int main(void){
 struct coxvbn_redlab_policy p,authp;struct coxvbn_module_registry registry;struct coxvbn_key_vault vault;struct coxvbn_module_ref redlab_module;static const int authority=1;const uint8_t key[]={0x42},proof[]={0x42};struct coxvbn_redlab_request aq;struct audit_log authlog={0,COXVBN_REDLAB_ALLOW};struct audit_log log={0,COXVBN_REDLAB_ALLOW};struct coxvbn_redlab_limits lim={2,1,10,100};
 struct coxvbn_redlab_rule r[2];struct coxvbn_redlab_request q;struct coxvbn_cidr c6;struct coxvbn_ip_addr a6;size_t before;
 r[0]=rule4(192,0,2,0,24,80,443,100,200,false);r[1]=rule4(192,0,2,128,25,80,443,100,200,true);
 assert(!coxvbn_redlab_policy_init(&p,r,2,&lim,false,NULL,&log));
 assert(coxvbn_redlab_policy_init(&authp,r,2,&lim,true,record,&authlog));
 coxvbn_key_vault_init(&vault,recovery_verify,NULL);
 coxvbn_key_vault_set_receiver_verifier(&vault,verify_key,NULL);
 coxvbn_module_registry_init(&registry,&authority);
 assert(coxvbn_module_register_domain(&registry,&authority,COXVBN_DOMAIN_REDLAB,
  COXVBN_CAP_REDLAB_DISCOVERY|COXVBN_CAP_REDLAB_ACTIVE_PROBE,
  COXVBN_CAP_REDLAB_DISCOVERY,&redlab_module));
 memset(&aq,0,sizeof(aq));aq.job_id=88;aq.target=ip4(192,0,2,10);aq.port=443;aq.now=100;aq.bytes=1;
 assert(coxvbn_redlab_policy_check_authorized(&authp,&registry,&redlab_module,COXVBN_REDLAB_OP_DISCOVERY,&aq)==COXVBN_REDLAB_DENY_CAPABILITY);
 assert(coxvbn_module_activate(&registry,&authority,&vault,&redlab_module,1,key,sizeof(key),proof,sizeof(proof)));
 assert(coxvbn_redlab_policy_check_authorized(&authp,&registry,&redlab_module,COXVBN_REDLAB_OP_DISCOVERY,&aq)==COXVBN_REDLAB_ALLOW);
 assert(coxvbn_redlab_policy_check_authorized(&authp,&registry,&redlab_module,COXVBN_REDLAB_OP_PASSIVE_CAPTURE,&aq)==COXVBN_REDLAB_DENY_CAPABILITY);
 assert(authlog.last==COXVBN_REDLAB_DENY_CAPABILITY);
 aq.job_id=89;aq.active=true;
 assert(coxvbn_redlab_policy_check_authorized(&authp,&registry,&redlab_module,COXVBN_REDLAB_OP_ACTIVE_PROBE,&aq)==COXVBN_REDLAB_DENY_CAPABILITY);
 assert(coxvbn_module_revoke(&registry,&authority,&vault,&redlab_module));
 aq.active=false;aq.job_id=90;
 assert(coxvbn_redlab_policy_check_authorized(&authp,&registry,&redlab_module,COXVBN_REDLAB_OP_DISCOVERY,&aq)==COXVBN_REDLAB_DENY_CAPABILITY);
 assert(coxvbn_redlab_policy_init(&p,r,2,&lim,false,record,&log));memset(&q,0,sizeof(q));q.job_id=1;q.target=ip4(192,0,2,10);q.port=443;q.now=100;q.bytes=10;
 assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_ALLOW);assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_ALLOW);assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_RATE);
 q.job_id=2;q.now=101;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_CONCURRENCY);q.now=99;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_TIME);
 q.now=101;q.active=true;q.job_id=1;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_ACTIVE_DISABLED);
 q.active=false;q.target=ip4(192,0,2,200);q.job_id=3;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_EXCLUDED);
 q.target=ip4(192,0,3,10);q.job_id=4;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_SCOPE);
 q.target=ip4(192,0,2,10);q.port=22;q.job_id=5;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_SCOPE);
 q.port=443;q.now=200;q.job_id=6;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_TIME);
 assert(coxvbn_redlab_policy_init(&p,r,2,&lim,false,record,&log));q.target=ip4(192,0,2,10);q.port=443;q.now=100;q.job_id=1;
 assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_ALLOW);assert(coxvbn_redlab_job_finish(&p,1));assert(!coxvbn_redlab_job_finish(&p,1));
 q.now=102;q.job_id=7;q.bytes=101;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_JOB_LIMIT);
 q.job_id=8;q.now=103;q.bytes=50;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_ALLOW);q.now=113;q.bytes=1;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_JOB_LIMIT);
 q.job_id=10;q.now=114;q.bytes=60;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_ALLOW);q.now=115;q.bytes=50;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_JOB_LIMIT);
 before=log.count;coxvbn_redlab_global_stop(&p);q.job_id=9;q.now=114;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_STOPPED);assert(log.count==before+2);
 memset(&c6,0,sizeof(c6));c6.family=COXVBN_IP_V6;c6.prefix=32;c6.network[0]=0x20;c6.network[1]=1;memset(&a6,0,sizeof(a6));a6.family=COXVBN_IP_V6;a6.bytes[0]=0x20;a6.bytes[1]=1;a6.bytes[15]=1;
 assert(coxvbn_redlab_cidr_valid(&c6));assert(coxvbn_redlab_cidr_contains(&c6,&a6));a6.bytes[1]=2;assert(!coxvbn_redlab_cidr_contains(&c6,&a6));c6.prefix=129;assert(!coxvbn_redlab_cidr_valid(&c6));
 r[0].target.prefix=33;assert(!coxvbn_redlab_policy_init(&p,r,1,&lim,false,record,&log));
 puts("COXVBN RedLab R1 policy tests: PASS");return 0;
}
