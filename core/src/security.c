#include "corecome/security.h"
#define SECURITY_AUDIT_CAPACITY 64u
#define SECURITY_KNOWN_OPERATIONS (SECURITY_OP_DISCOVER|SECURITY_OP_CONNECT|SECURITY_OP_INSPECT|SECURITY_OP_CAPTURE)
struct security_scope {
 uint64_t scope_id,requester_id,target_id,expires_at;
 uint32_t allowed_operations,action_limit,actions_used;
 bool active;
};
static struct security_scope current_scope;
static struct security_audit_record audit_log[SECURITY_AUDIT_CAPACITY];
static uint64_t audit_sequence;
static uint32_t audit_count;
static bool emergency_stopped;
static void audit_append(uint32_t event,uint64_t scope,uint64_t target,
 uint32_t operation,uint64_t now) {
 uint32_t slot=(uint32_t)(audit_sequence%SECURITY_AUDIT_CAPACITY);
 struct security_audit_record *r=&audit_log[slot];
 r->sequence=++audit_sequence;r->scope_id=scope;r->target_id=target;
 r->tick=now;r->operation=operation;r->event=event;
 if(audit_count<SECURITY_AUDIT_CAPACITY)++audit_count;
}
void security_init(void) {
 uint32_t i;
 current_scope.scope_id=0;current_scope.requester_id=0;current_scope.target_id=0;
 current_scope.expires_at=0;current_scope.allowed_operations=0;
 current_scope.action_limit=0;current_scope.actions_used=0;current_scope.active=false;
 for(i=0;i<SECURITY_AUDIT_CAPACITY;i++){
  audit_log[i].sequence=0;audit_log[i].scope_id=0;audit_log[i].target_id=0;
  audit_log[i].tick=0;audit_log[i].operation=0;audit_log[i].event=0;
 }
 audit_sequence=0;audit_count=0;emergency_stopped=false;
}
bool security_monitor_install_grant(uint64_t grant,uint64_t requester,
 uint64_t target,uint32_t ops,uint64_t expiry,uint32_t limit,uint64_t now) {
 if(emergency_stopped||current_scope.active||!grant||!requester||!target||
 !ops||(ops&~SECURITY_KNOWN_OPERATIONS)||!limit||expiry<=now){
  audit_append(SECURITY_AUDIT_DENY,grant,target,0,now);return false;
 }
 current_scope.scope_id=grant;current_scope.requester_id=requester;
 current_scope.target_id=target;current_scope.expires_at=expiry;
 current_scope.allowed_operations=ops;current_scope.action_limit=limit;
 current_scope.actions_used=0;current_scope.active=true;
 audit_append(SECURITY_AUDIT_GRANT,grant,target,0,now);return true;
}
bool security_authorize(uint64_t grant,uint64_t requester,uint64_t target,
 uint32_t op,uint64_t now) {
 bool ok=!emergency_stopped&&current_scope.active&&grant==current_scope.scope_id&&
 requester==current_scope.requester_id&&target==current_scope.target_id&&op&&
 !(op&(op-1u))&&(current_scope.allowed_operations&op)==op&&
 now<current_scope.expires_at&&current_scope.actions_used<current_scope.action_limit;
 if(!ok){audit_append(SECURITY_AUDIT_DENY,grant,target,op,now);return false;}
 ++current_scope.actions_used;audit_append(SECURITY_AUDIT_ALLOW,grant,target,op,now);
 return true;
}
void security_scope_revoke(uint64_t now){
 if(current_scope.active)audit_append(SECURITY_AUDIT_REVOKE,current_scope.scope_id,
 current_scope.target_id,0,now);
 current_scope.active=false;current_scope.allowed_operations=0;
}
void security_emergency_stop(uint64_t now){
 if(!emergency_stopped)audit_append(SECURITY_AUDIT_STOP,current_scope.scope_id,
 current_scope.target_id,0,now);
 emergency_stopped=true;current_scope.active=false;current_scope.allowed_operations=0;
}
bool security_is_stopped(void){return emergency_stopped;}
uint32_t security_audit_count(void){return audit_count;}
bool security_audit_read(uint32_t n,struct security_audit_record *out){
 uint32_t newest;
 if(!out||n>=audit_count)return false;
 newest=(uint32_t)((audit_sequence-1u)%SECURITY_AUDIT_CAPACITY);
 *out=audit_log[(newest+SECURITY_AUDIT_CAPACITY-(n%SECURITY_AUDIT_CAPACITY))%
 SECURITY_AUDIT_CAPACITY];return true;
}
