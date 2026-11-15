#include "corecome/security.h"

#define SECURITY_AUDIT_CAPACITY 64u

static struct security_scope current_scope;
static struct security_audit_record audit_log[SECURITY_AUDIT_CAPACITY];
static uint64_t audit_sequence;
static uint32_t audit_count;
static bool emergency_stopped;

#define SECURITY_KNOWN_OPERATIONS (SECURITY_OP_DISCOVER | SECURITY_OP_CONNECT | \\
                                   SECURITY_OP_INSPECT | SECURITY_OP_CAPTURE)

static void audit_append(uint32_t event, uint64_t scope_id,
                         uint64_t target_id, uint32_t operation,
                         uint64_t now)
{
    uint32_t slot = (uint32_t)(audit_sequence % SECURITY_AUDIT_CAPACITY);
    struct security_audit_record *record = &audit_log[slot];

    record->sequence = ++audit_sequence;
    record->scope_id = scope_id;
    record->target_id = target_id;
    record->tick = now;
    record->operation = operation;
    record->event = event;

    if (audit_count < SECURITY_AUDIT_CAPACITY) {
        ++audit_count;
    }
}

void security_init(void)
{
    uint32_t i;

    current_scope.scope_id = 0;
    current_scope.target_id = 0;
    current_scope.expires_at = 0;
    current_scope.allowed_operations = 0;
    current_scope.action_limit = 0;
    current_scope.actions_used = 0;
    current_scope.active = false;

    for (i = 0; i < SECURITY_AUDIT_CAPACITY; ++i) {
        audit_log[i].sequence = 0;
        audit_log[i].scope_id = 0;
        audit_log[i].target_id = 0;
        audit_log[i].tick = 0;
        audit_log[i].operation = 0;
        audit_log[i].event = 0;
    }

    audit_sequence = 0;
    audit_count = 0;
    emergency_stopped = false;
}

bool security_scope_grant(uint64_t scope_id, uint64_t target_id,
                          uint32_t allowed_operations,
                          uint64_t expires_at, uint32_t action_limit,
                          uint64_t now)
{
    if (emergency_stopped || current_scope.active ||
        scope_id == 0 || target_id == 0 ||
        allowed_operations == 0 ||
        (allowed_operations & ~SECURITY_KNOWN_OPERATIONS) != 0 ||
        action_limit == 0 ||
        expires_at <= now) {
        audit_append(SECURITY_AUDIT_DENY, scope_id, target_id, 0, now);
        return false;
    }

    current_scope.scope_id = scope_id;
    current_scope.target_id = target_id;
    current_scope.expires_at = expires_at;
    current_scope.allowed_operations = allowed_operations;
    current_scope.action_limit = action_limit;
    current_scope.actions_used = 0;
    current_scope.active = true;

    audit_append(SECURITY_AUDIT_GRANT, scope_id, target_id, 0, now);
    return true;
}

bool security_authorize(uint64_t scope_id, uint64_t target_id,
                        uint32_t operation, uint64_t now)
{
    bool permitted = !emergency_stopped &&
                     current_scope.active &&
                     scope_id == current_scope.scope_id &&
                     target_id == current_scope.target_id &&
                     operation != 0 &&
                     (operation & (operation - 1u)) == 0 &&
                     (current_scope.allowed_operations & operation) == operation &&
                     now < current_scope.expires_at &&
                     current_scope.actions_used < current_scope.action_limit;

    if (!permitted) {
        audit_append(SECURITY_AUDIT_DENY, scope_id, target_id, operation, now);
        return false;
    }

    ++current_scope.actions_used;
    audit_append(SECURITY_AUDIT_ALLOW, scope_id, target_id, operation, now);
    return true;
}

void security_scope_revoke(uint64_t now)
{
    if (current_scope.active) {
        audit_append(SECURITY_AUDIT_REVOKE, current_scope.scope_id,
                     current_scope.target_id, 0, now);
    }
    current_scope.active = false;
    current_scope.allowed_operations = 0;
}

void security_emergency_stop(uint64_t now)
{
    if (!emergency_stopped) {
        audit_append(SECURITY_AUDIT_STOP, current_scope.scope_id,
                     current_scope.target_id, 0, now);
    }
    emergency_stopped = true;
    current_scope.active = false;
    current_scope.allowed_operations = 0;
}

bool security_is_stopped(void)
{
    return emergency_stopped;
}

uint32_t security_audit_count(void)
{
    return audit_count;
}

bool security_audit_read(uint32_t newest_index,
                         struct security_audit_record *out)
{
    uint32_t newest_slot;

    if (out == 0 || newest_index >= audit_count) {
        return false;
    }

    newest_slot = (uint32_t)((audit_sequence - 1u) % SECURITY_AUDIT_CAPACITY);
    *out = audit_log[(newest_slot + SECURITY_AUDIT_CAPACITY -
                      (newest_index % SECURITY_AUDIT_CAPACITY)) %
                     SECURITY_AUDIT_CAPACITY];
    return true;
}
