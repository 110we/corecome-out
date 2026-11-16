#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "redlab_policy.h"
struct audit_log { size_t count; enum coxvbn_redlab_decision last; uint64_t module_id,module_generation; uint32_t operation; };
static void record(const struct coxvbn_redlab_audit_event *e,void *ctx){struct audit_log *l=ctx;assert(e&&e->version==COXVBN_REDLAB_AUDIT_VERSION);l->count++;l->last=e->decision;l->module_id=e->module_id;l->module_generation=e->module_generation;l->operation=e->operation;}
struct auth_state { bool allowed; uint64_t id,generation; };
static bool authorize(enum coxvbn_redlab_operation op,uint64_t *id,uint64_t *generation,void *ctx){struct auth_state *s=ctx;if(!s->allowed)return false;if(op!=COXVBN_REDLAB_OP_DISCOVERY&&op!=COXVBN_REDLAB_OP_PASSIVE_CAPTURE&&op!=COXVBN_REDLAB_OP_ACTIVE_PROBE)return false;*id=s->id;*generation=s->generation;return true;}
static struct coxvbn_redlab_rule rule4(uint8_t a,uint8_t b,uint8_t c,uint8_t d,uint8_t pre,uint16_t lo,uint16_t hi,uint64_t from,uint64_t to,bool ex){struct coxvbn_redlab_rule r;memset(&r,0,sizeof(r));r.target.family=COXVBN_IP_V4;r.target.prefix=pre;r.target.network[0]=a;r.target.network[1]=b;r.target.network[2]=c;r.target.network[3]=d;r.first_port=lo;r.last_port=hi;r.not_before=from;r.expires_at=to;r.exclude=ex;return r;}
static struct coxvbn_ip_addr ip4(uint8_t a,uint8_t b,uint8_t c,uint8_t d){struct coxvbn_ip_addr x;memset(&x,0,sizeof(x));x.family=COXVBN_IP_V4;x.bytes[0]=a;x.bytes[1]=b;x.bytes[2]=c;x.bytes[3]=d;return x;}
int main(void){
 struct coxvbn_redlab_policy p;struct coxvbn_redlab_limits limits={2,1,10,100};struct coxvbn_redlab_rule rules[2];struct coxvbn_redlab_request q;struct audit_log log={0,COXVBN_REDLAB_ALLOW,0,0,0};struct auth_state auth={true,41,3};struct coxvbn_redlab_authority authority={authorize,&auth};struct coxvbn_cidr c6;struct coxvbn_ip_addr a6;size_t before;
 rules[0]=rule4(192,0,2,0,24,80,443,100,200,false);rules[1]=rule4(192,0,2,128,25,80,443,100,200,true);assert(coxvbn_redlab_policy_init(&p,rules,2,&limits,false,record,&log));
 memset(&q,0,sizeof(q));q.job_id=1;q.target=ip4(192,0,2,10);q.port=443;q.now=100;q.bytes=10;
 auth.allowed=false;assert(coxvbn_redlab_policy_check_authorized(&p,&authority,COXVBN_REDLAB_OP_DISCOVERY,&q)==COXVBN_REDLAB_DENY_CAPABILITY);assert(log.module_id==0U);
 auth.allowed=true;assert(coxvbn_redlab_policy_check_authorized(&p,&authority,COXVBN_REDLAB_OP_DISCOVERY,&q)==COXVBN_REDLAB_ALLOW);assert(log.module_id==41U&&log.module_generation==3U&&log.operation==COXVBN_REDLAB_OP_DISCOVERY);
 q.job_id=2;q.active=true;assert(coxvbn_redlab_policy_check_authorized(&p,&authority,COXVBN_REDLAB_OP_DISCOVERY,&q)==COXVBN_REDLAB_DENY_CAPABILITY);q.active=false;q.job_id=3;assert(coxvbn_redlab_policy_check_authorized(&p,&authority,COXVBN_REDLAB_OP_PASSIVE_CAPTURE,&q)==COXVBN_REDLAB_DENY_CONCURRENCY);
 assert(coxvbn_redlab_policy_init(&p,rules,2,&limits,false,record,&log));q.job_id=1;q.target=ip4(192,0,2,10);q.port=443;q.now=100;q.bytes=10;
 assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_ALLOW);assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_ALLOW);assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_RATE);
 q.job_id=2;q.now=101;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_CONCURRENCY);q.now=99;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_TIME);
 q.now=101;q.active=true;q.job_id=1;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_ACTIVE_DISABLED);q.active=false;q.target=ip4(192,0,2,200);q.job_id=3;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_EXCLUDED);
 q.target=ip4(192,0,3,10);q.job_id=4;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_SCOPE);q.target=ip4(192,0,2,10);q.port=22;q.job_id=5;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_SCOPE);
 q.port=443;q.now=200;q.job_id=6;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_TIME);
 assert(coxvbn_redlab_policy_init(&p,rules,2,&limits,false,record,&log));q.target=ip4(192,0,2,10);q.port=443;q.now=100;q.job_id=1;q.bytes=10;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_ALLOW);assert(coxvbn_redlab_job_finish(&p,1));assert(!coxvbn_redlab_job_finish(&p,1));
 q.now=102;q.job_id=7;q.bytes=101;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_JOB_LIMIT);q.job_id=8;q.now=103;q.bytes=50;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_ALLOW);q.now=113;q.bytes=1;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_JOB_LIMIT);
 q.job_id=10;q.now=114;q.bytes=60;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_ALLOW);q.now=115;q.bytes=50;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_JOB_LIMIT);before=log.count;coxvbn_redlab_global_stop(&p);q.job_id=9;q.now=114;assert(coxvbn_redlab_policy_check(&p,&q)==COXVBN_REDLAB_DENY_STOPPED);assert(log.count==before+2);
 memset(&c6,0,sizeof(c6));c6.family=COXVBN_IP_V6;c6.prefix=32;c6.network[0]=0x20;c6.network[1]=1;memset(&a6,0,sizeof(a6));a6.family=COXVBN_IP_V6;a6.bytes[0]=0x20;a6.bytes[1]=1;a6.bytes[15]=1;assert(coxvbn_redlab_cidr_valid(&c6));assert(coxvbn_redlab_cidr_contains(&c6,&a6));a6.bytes[1]=2;assert(!coxvbn_redlab_cidr_contains(&c6,&a6));c6.prefix=129;assert(!coxvbn_redlab_cidr_valid(&c6));rules[0].target.prefix=33;assert(!coxvbn_redlab_policy_init(&p,rules,1,&limits,false,record,&log));
 puts("RedLab standalone scope, authority, audit and safety tests: PASS");return 0;
}
