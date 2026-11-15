#ifndef CORECOME_SECURITY_H
#define CORECOME_SECURITY_H
#include <stdbool.h>
#include <stdint.h>
enum security_operation {
 SECURITY_OP_DISCOVER=1u<<0, SECURITY_OP_CONNECT=1u<<1,
 SECURITY_OP_INSPECT=1u<<2, SECURITY_OP_CAPTURE=1u<<3
};
enum security_audit_event {
 SECURITY_AUDIT_GRANT=1, SECURITY_AUDIT_ALLOW, SECURITY_AUDIT_DENY,
 SECURITY_AUDIT_REVOKE, SECURITY_AUDIT_STOP
};
struct security_audit_record {
 uint64_t sequence, scope_id, target_id, tick;
 uint32_t operation, event;
};
void security_init(void);
bool security_monitor_install_grant(uint64_t grant_id,uint64_t requester_id,
 uint64_t target_id,uint32_t operations,uint64_t expires_at,
 uint32_t action_limit,uint64_t now);
bool security_authorize(uint64_t grant_id,uint64_t requester_id,
 uint64_t target_id,uint32_t operation,uint64_t now);
void security_scope_revoke(uint64_t now);
void security_emergency_stop(uint64_t now);
bool security_is_stopped(void);
uint32_t security_audit_count(void);
bool security_audit_read(uint32_t newest_index,struct security_audit_record *out);
#endif
