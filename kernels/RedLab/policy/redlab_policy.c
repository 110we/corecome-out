#include "redlab_policy.h"
static size_t address_bytes(uint8_t f){return f==COXVBN_IP_V4?4U:(f==COXVBN_IP_V6?16U:0U);}
bool coxvbn_redlab_cidr_valid(const struct coxvbn_cidr *c){
 size_t n,full,rem,i;if(!c)return false;n=address_bytes(c->family);
 if(!n||c->prefix>n*8U)return false;
 full=c->prefix/8U;rem=c->prefix%8U;
 if(rem&&(c->network[full]&(uint8_t)((1U<<(8U-rem))-1U)))return false;
 for(i=full+(rem?1U:0U);i<n;i++)if(c->network[i])return false;
 for(i=n;i<16U;i++)if(c->network[i])return false;
 return true;
}
bool coxvbn_redlab_cidr_contains(const struct coxvbn_cidr *c,const struct coxvbn_ip_addr *a){
 size_t full,rem;if(!coxvbn_redlab_cidr_valid(c)||!a||a->family!=c->family)return false;
 full=c->prefix/8U;rem=c->prefix%8U;
 for(size_t i=0;i<full;i++)if(c->network[i]!=a->bytes[i])return false;
 if(rem){uint8_t m=(uint8_t)(0xffU<<(8U-rem));if((c->network[full]&m)!=(a->bytes[full]&m))return false;}
 return true;
}
static bool address_valid(const struct coxvbn_ip_addr *a){
 size_t n=a?address_bytes(a->family):0U;if(!n)return false;
 for(size_t i=n;i<16U;i++)if(a->bytes[i])return false;
 return true;
}
static bool address_equal(const struct coxvbn_ip_addr *a,const struct coxvbn_ip_addr *b){
 if(a->family!=b->family)return false;
 size_t n=address_bytes(a->family);
 for(size_t i=0;i<n;i++)if(a->bytes[i]!=b->bytes[i])return false;
 return true;
}
static void audit(struct coxvbn_redlab_policy *p,const struct coxvbn_redlab_request *q,enum coxvbn_redlab_decision d,const struct coxvbn_redlab_audit_context *identity){
 struct coxvbn_redlab_audit_event e;if(!p->audit)return;e.version=COXVBN_REDLAB_AUDIT_VERSION;
 e.module_id=identity?identity->module_id:0U;e.module_generation=identity?identity->module_generation:0U;
 e.operation=identity?(uint32_t)identity->operation:UINT32_MAX;
 e.job_id=q?q->job_id:0U;e.monotonic_seconds=q?q->now:p->last_observed_time;
 if(q)e.target=q->target;else{e.target.family=0U;for(size_t i=0;i<16U;i++)e.target.bytes[i]=0U;}
 e.port=q?q->port:0U;e.decision=d;e.active=q&&q->active;p->audit(&e,p->audit_context);
}
static enum coxvbn_redlab_decision deny(struct coxvbn_redlab_policy *p,const struct coxvbn_redlab_request *q,enum coxvbn_redlab_decision d,const struct coxvbn_redlab_audit_context *identity){audit(p,q,d,identity);return d;}
bool coxvbn_redlab_policy_init(struct coxvbn_redlab_policy *p,const struct coxvbn_redlab_rule *r,size_t n,const struct coxvbn_redlab_limits *l,bool active,coxvbn_redlab_audit_fn fn,void *ctx){
 if(!p||!l||!fn||n>COXVBN_REDLAB_MAX_RULES||(n&&!r)||!l->requests_per_second||!l->max_concurrent_jobs_per_target||!l->max_job_duration_seconds||!l->max_job_bytes)return false;
 for(size_t i=0;i<n;i++)if(!coxvbn_redlab_cidr_valid(&r[i].target)||!r[i].first_port||r[i].first_port>r[i].last_port||r[i].not_before>=r[i].expires_at)return false;
 p->rule_count=n;for(size_t i=0;i<n;i++)p->rules[i]=r[i];p->limits=*l;
 for(size_t i=0;i<COXVBN_REDLAB_MAX_TARGETS;i++){p->targets[i].used=false;p->targets[i].rate_window=0;p->targets[i].requests_in_window=0;p->targets[i].active_jobs=0;}
 for(size_t i=0;i<COXVBN_REDLAB_MAX_JOBS;i++)p->jobs[i].used=false;
 p->audit=fn;p->audit_context=ctx;p->last_observed_time=0;p->time_initialized=false;p->active_enabled=active;p->stopped=false;return true;
}
static enum coxvbn_redlab_decision scope(const struct coxvbn_redlab_policy *p,const struct coxvbn_redlab_request *q){
 bool allowed=false,expired=false;
 for(size_t i=0;i<p->rule_count;i++){const struct coxvbn_redlab_rule *r=&p->rules[i];
 if(q->port<r->first_port||q->port>r->last_port||!coxvbn_redlab_cidr_contains(&r->target,&q->target))continue;
 if(r->exclude)return COXVBN_REDLAB_DENY_EXCLUDED;
 if(q->now<r->not_before||q->now>=r->expires_at){expired=true;continue;}allowed=true;}
 if(allowed)return COXVBN_REDLAB_ALLOW;
 return expired?COXVBN_REDLAB_DENY_TIME:COXVBN_REDLAB_DENY_SCOPE;
}
static size_t find_target(struct coxvbn_redlab_policy *p,const struct coxvbn_ip_addr *a,bool *ok){
 size_t free_i=COXVBN_REDLAB_MAX_TARGETS;
 for(size_t i=0;i<COXVBN_REDLAB_MAX_TARGETS;i++){if(p->targets[i].used&&address_equal(&p->targets[i].address,a)){*ok=true;return i;}if(!p->targets[i].used&&free_i==COXVBN_REDLAB_MAX_TARGETS)free_i=i;}
 *ok=free_i!=COXVBN_REDLAB_MAX_TARGETS;return free_i;
}
static size_t find_job(struct coxvbn_redlab_policy *p,uint64_t id){for(size_t i=0;i<COXVBN_REDLAB_MAX_JOBS;i++)if(p->jobs[i].used&&p->jobs[i].job_id==id)return i;return COXVBN_REDLAB_MAX_JOBS;}
static size_t free_job(struct coxvbn_redlab_policy *p){for(size_t i=0;i<COXVBN_REDLAB_MAX_JOBS;i++)if(!p->jobs[i].used)return i;return COXVBN_REDLAB_MAX_JOBS;}
bool coxvbn_redlab_job_finish(struct coxvbn_redlab_policy *p,uint64_t id){
 if(!p||!id)return false;
 size_t s=find_job(p,id);
 if(s==COXVBN_REDLAB_MAX_JOBS)return false;
 struct coxvbn_redlab_job_state *j=&p->jobs[s];if(j->target_slot>=COXVBN_REDLAB_MAX_TARGETS)return false;
 struct coxvbn_redlab_target_state *t=&p->targets[j->target_slot];if(t->active_jobs)--t->active_jobs;j->used=false;return true;
}
static enum coxvbn_redlab_decision policy_check_scoped(struct coxvbn_redlab_policy *p,const struct coxvbn_redlab_request *q,const struct coxvbn_redlab_audit_context *identity){
 enum coxvbn_redlab_decision d;size_t ts,js;bool found;struct coxvbn_redlab_target_state *t;struct coxvbn_redlab_job_state *j;
 if(!p)return COXVBN_REDLAB_DENY_INVALID;
 if(!q||!q->job_id||!address_valid(&q->target)||!q->port)return deny(p,q,COXVBN_REDLAB_DENY_INVALID,identity);
 if(p->stopped)return deny(p,q,COXVBN_REDLAB_DENY_STOPPED,identity);
 if(p->time_initialized&&q->now<p->last_observed_time)return deny(p,q,COXVBN_REDLAB_DENY_TIME,identity);
 p->last_observed_time=q->now;p->time_initialized=true;
 if(q->active&&!p->active_enabled)return deny(p,q,COXVBN_REDLAB_DENY_ACTIVE_DISABLED,identity);
 d=scope(p,q);if(d!=COXVBN_REDLAB_ALLOW)return deny(p,q,d,identity);
 js=find_job(p,q->job_id);
 if(js!=COXVBN_REDLAB_MAX_JOBS){size_t old=p->jobs[js].target_slot;if(old>=COXVBN_REDLAB_MAX_TARGETS||!address_equal(&p->targets[old].address,&q->target))return deny(p,q,COXVBN_REDLAB_DENY_INVALID,identity);}
 ts=find_target(p,&q->target,&found);if(!found)return deny(p,q,COXVBN_REDLAB_DENY_CAPACITY,identity);
 t=&p->targets[ts];if(!t->used){t->used=true;t->address=q->target;t->rate_window=q->now;t->requests_in_window=0;t->active_jobs=0;}
 if(q->now!=t->rate_window){t->rate_window=q->now;t->requests_in_window=0;}
 if(t->requests_in_window>=p->limits.requests_per_second)return deny(p,q,COXVBN_REDLAB_DENY_RATE,identity);
 if(js==COXVBN_REDLAB_MAX_JOBS){
 if(t->active_jobs>=p->limits.max_concurrent_jobs_per_target)return deny(p,q,COXVBN_REDLAB_DENY_CONCURRENCY,identity);
 js=free_job(p);if(js==COXVBN_REDLAB_MAX_JOBS)return deny(p,q,COXVBN_REDLAB_DENY_CAPACITY,identity);
 j=&p->jobs[js];j->used=true;j->job_id=q->job_id;j->started_at=q->now;j->bytes_used=0;j->target_slot=ts;++t->active_jobs;
 }else j=&p->jobs[js];
 if(q->now<j->started_at||q->now-j->started_at>=p->limits.max_job_duration_seconds||j->bytes_used>p->limits.max_job_bytes||q->bytes>p->limits.max_job_bytes-j->bytes_used){(void)coxvbn_redlab_job_finish(p,q->job_id);return deny(p,q,COXVBN_REDLAB_DENY_JOB_LIMIT,identity);}
 ++t->requests_in_window;j->bytes_used+=q->bytes;audit(p,q,COXVBN_REDLAB_ALLOW,identity);return COXVBN_REDLAB_ALLOW;
}
void coxvbn_redlab_global_stop(struct coxvbn_redlab_policy *p){
 if(!p||p->stopped)return;
 p->stopped=true;
 audit(p,NULL,COXVBN_REDLAB_DENY_STOPPED,NULL);
 for(size_t i=0;i<COXVBN_REDLAB_MAX_JOBS;i++)if(p->jobs[i].used)(void)coxvbn_redlab_job_finish(p,p->jobs[i].job_id);
 for(size_t i=0;i<COXVBN_REDLAB_MAX_TARGETS;i++){p->targets[i].active_jobs=0;p->targets[i].used=false;}
}


#ifdef COXVBN_REDLAB_TESTING
enum coxvbn_redlab_decision coxvbn_redlab_policy_check(
 struct coxvbn_redlab_policy *p,const struct coxvbn_redlab_request *q)
{
 return policy_check_scoped(p,q,NULL);
}
#endif

enum coxvbn_redlab_decision coxvbn_redlab_policy_check_authorized(
 struct coxvbn_redlab_policy *p,
 const struct coxvbn_redlab_authority *authority,
 enum coxvbn_redlab_operation operation,
 const struct coxvbn_redlab_request *request)
{
 struct coxvbn_redlab_audit_context identity;
 identity.module_id=0U;
 identity.module_generation=0U;
 identity.operation=operation;
 if(!p)return COXVBN_REDLAB_DENY_INVALID;
 if(!authority||!authority->authorize||
    !authority->authorize(operation,&identity.module_id,
                          &identity.module_generation,authority->context)||
    identity.module_id==0U||identity.module_generation==0U)
  return deny(p,request,COXVBN_REDLAB_DENY_CAPABILITY,&identity);
 if(operation!=COXVBN_REDLAB_OP_DISCOVERY&&
    operation!=COXVBN_REDLAB_OP_PASSIVE_CAPTURE&&
    operation!=COXVBN_REDLAB_OP_ACTIVE_PROBE)
  return deny(p,request,COXVBN_REDLAB_DENY_CAPABILITY,&identity);
 if((operation==COXVBN_REDLAB_OP_ACTIVE_PROBE)!=
    (request!=NULL&&request->active))
  return deny(p,request,COXVBN_REDLAB_DENY_CAPABILITY,&identity);
 return policy_check_scoped(p,request,&identity);
}
