#include "corecome/security_plugin.h"
#include "security_internal.h"

static struct security_plugin_request pending;
static enum security_plugin_request_state state;

void security_plugin_init(void) { pending.request_id = 0; state = SECURITY_PLUGIN_EMPTY; }

bool security_plugin_submit(const struct security_plugin_request *r)
{
    if (!r || state == SECURITY_PLUGIN_PENDING || state == SECURITY_PLUGIN_APPROVED ||
        !r->request_id || !r->requester_id || !r->target_id ||
        !r->requested_operations || !r->requested_action_limit) return false;
    pending = *r;
    state = SECURITY_PLUGIN_PENDING;
    return true;
}

bool security_plugin_user_decision(uint64_t id, bool approved, uint64_t now)
{
    if (state != SECURITY_PLUGIN_PENDING || id != pending.request_id) return false;
    if (!approved || pending.requested_expires_at <= now) {
        state = SECURITY_PLUGIN_DENIED;
        return !approved;
    }
    if (!security_monitor_install_grant(pending.request_id, pending.requester_id,
        pending.target_id, pending.requested_operations,
        pending.requested_expires_at, pending.requested_action_limit, now)) {
        state = SECURITY_PLUGIN_DENIED;
        return false;
    }
    state = SECURITY_PLUGIN_APPROVED;
    return true;
}

bool security_plugin_authorize(uint64_t grant, uint64_t requester,
                              uint64_t target, uint32_t op, uint64_t now)
{
    if (state != SECURITY_PLUGIN_APPROVED || grant != pending.request_id) return false;
    return security_authorize(grant, requester, target, op, now);
}

void security_plugin_revoke(uint64_t now)
{
    security_scope_revoke(now);
    state = SECURITY_PLUGIN_REVOKED;
}
