#ifndef CORECOME_SECURITY_PLUGIN_H
#define CORECOME_SECURITY_PLUGIN_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Trusted Security Plugin contract.
 *
 * The plugin owns permission policy and user consent. The kernel security
 * reference monitor remains responsible for enforcing grants on every
 * privileged operation. This is an interface contract only: dynamic plugin
 * loading, authenticated tool identities and user consent UI are not yet
 * implemented in the current M0/M1 kernel.
 */

enum security_plugin_request_state {
    SECURITY_PLUGIN_PENDING = 0,
    SECURITY_PLUGIN_APPROVED,
    SECURITY_PLUGIN_DENIED,
    SECURITY_PLUGIN_EXPIRED,
    SECURITY_PLUGIN_REVOKED
};

struct security_plugin_request {
    uint64_t request_id;
    uint64_t requester_id;
    uint64_t target_id;
    uint64_t requested_expires_at;
    uint32_t requested_operations;
    uint32_t requested_action_limit;
};

struct security_plugin_grant {
    uint64_t grant_id;
    uint64_t requester_id;
    uint64_t target_id;
    uint64_t expires_at;
    uint32_t allowed_operations;
    uint32_t action_limit;
};

/*
 * Future trusted-plugin API:
 * - submit_request queues a request for Security Plugin consent.
 * - user_decision records an explicit local-user decision.
 * - get_grant returns a grant only after approval.
 * - revoke invalidates the grant at the kernel reference monitor.
 *
 * Implementations must fail closed. No tool may call the kernel grant
 * primitive directly; only the trusted Security Plugin may issue grants.
 */

#endif
