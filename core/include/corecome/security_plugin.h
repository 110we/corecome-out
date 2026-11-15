#ifndef CORECOME_SECURITY_PLUGIN_H
#define CORECOME_SECURITY_PLUGIN_H
#include <stdbool.h>
#include <stdint.h>
#include "corecome/security.h"
enum security_plugin_request_state {
 SECURITY_PLUGIN_EMPTY=0, SECURITY_PLUGIN_PENDING, SECURITY_PLUGIN_APPROVED,
 SECURITY_PLUGIN_DENIED, SECURITY_PLUGIN_REVOKED
};
struct security_plugin_request {
 uint64_t request_id, requester_id, target_id, requested_expires_at;
 uint32_t requested_operations, requested_action_limit;
};
void security_plugin_init(void);
bool security_plugin_submit(const struct security_plugin_request *request);
bool security_plugin_user_decision(uint64_t request_id,bool approved,uint64_t now);
bool security_plugin_authorize(uint64_t grant_id,uint64_t requester_id,
 uint64_t target_id,uint32_t operation,uint64_t now);
void security_plugin_revoke(uint64_t now);
#endif
