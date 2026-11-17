#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "execution_context.h"
#include "module_registry.h"
#include "redlab_policy.h"

struct audit_log {
    size_t count;
    struct coxvbn_redlab_audit_event last;
};

static struct coxvbn_module_ref current_module;
static bool provider_available;

static void record_audit(const struct coxvbn_redlab_audit_event *event,
                         void *context)
{
    struct audit_log *log = context;
    assert(event != NULL);
    assert(event->version == COXVBN_REDLAB_AUDIT_VERSION);
    log->last = *event;
    ++log->count;
}

static bool current_module_provider(struct coxvbn_module_ref *out,
                                     void *context)
{
    const bool *available = context;
    if (out == NULL || available == NULL || !*available)
        return false;
    *out = current_module;
    return true;
}

static bool recovery_proof(const uint8_t *proof, size_t length, void *context)
{
    (void)context;
    return proof != NULL && length == 1U && proof[0] == 0x5aU;
}

static bool receiver_proof(const uint8_t *key, size_t key_length,
                           const uint8_t *proof, size_t proof_length,
                           void *context)
{
    (void)context;
    return key != NULL && proof != NULL && key_length == 1U &&
           proof_length == 1U && key[0] == proof[0];
}

static struct coxvbn_ip_addr target_address(void)
{
    struct coxvbn_ip_addr address;
    memset(&address, 0, sizeof(address));
    address.family = COXVBN_IP_V4;
    address.bytes[0] = 192U;
    address.bytes[1] = 0U;
    address.bytes[2] = 2U;
    address.bytes[3] = 10U;
    return address;
}

static void assert_audit(const struct audit_log *log,
                         enum coxvbn_redlab_decision decision,
                         uint64_t module_id, uint64_t generation,
                         uint32_t operation)
{
    assert(log->last.decision == decision);
    assert(log->last.module_id == module_id);
    assert(log->last.module_generation == generation);
    assert(log->last.operation == operation);
}

static void activate_module(struct coxvbn_module_registry *registry,
                            const void *authority,
                            struct coxvbn_key_vault *vault,
                            enum coxvbn_module_domain domain,
                            coxvbn_capability_t requested,
                            coxvbn_capability_t granted,
                            uint64_t receiver_id,
                            const uint8_t *key,
                            const uint8_t *proof,
                            struct coxvbn_module_ref *out)
{
    assert(coxvbn_module_register_domain(registry, authority, domain,
                                         requested, granted, out));
    assert(coxvbn_module_activate(registry, authority, vault, out,
                                  receiver_id, key, 1U, proof, 1U));
}

int main(void)
{
    static const int loader_authority = 0x51;
    const uint8_t key[] = {0x42U};
    const uint8_t proof[] = {0x42U};
    struct coxvbn_module_registry registry;
    struct coxvbn_key_vault vault;
    struct coxvbn_redlab_policy policy;
    struct coxvbn_redlab_rule rule;
    struct coxvbn_redlab_limits limits = {100U, 8U, 60U, 4096U};
    struct coxvbn_redlab_request request;
    struct coxvbn_module_ref discovery, passive, active, daily, no_grant;
    struct audit_log log;
    enum coxvbn_redlab_operation invalid_operation =
        (enum coxvbn_redlab_operation)99;

    memset(&log, 0, sizeof(log));
    memset(&rule, 0, sizeof(rule));
    rule.target.family = COXVBN_IP_V4;
    rule.target.prefix = 24U;
    rule.target.network[0] = 192U;
    rule.target.network[1] = 0U;
    rule.target.network[2] = 2U;
    rule.first_port = 443U;
    rule.last_port = 443U;
    rule.not_before = 100U;
    rule.expires_at = 200U;

    assert(coxvbn_redlab_policy_init(&policy, &rule, 1U, &limits, true,
                                     record_audit, &log));
    coxvbn_key_vault_init(&vault, recovery_proof, NULL);
    coxvbn_key_vault_set_receiver_verifier(&vault, receiver_proof, NULL);
    coxvbn_module_registry_init(&registry, &loader_authority);

    memset(&request, 0, sizeof(request));
    request.job_id = 1U;
    request.target = target_address();
    request.port = 443U;
    request.now = 101U;
    request.bytes = 1U;

    /* No provider means no caller identity and therefore no admission. */
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_DISCOVERY,
        &request) == COXVBN_REDLAB_DENY_CAPABILITY);
    assert_audit(&log, COXVBN_REDLAB_DENY_CAPABILITY, 0U, 0U,
                 COXVBN_REDLAB_OP_DISCOVERY);

    provider_available = true;
    assert(coxvbn_execution_context_install_provider(
        current_module_provider, &provider_available));
    assert(!coxvbn_execution_context_install_provider(
        current_module_provider, &provider_available));

    /* A pending module is never admitted, even with the right grant mask. */
    assert(coxvbn_module_register_domain(
        &registry, &loader_authority, COXVBN_DOMAIN_REDLAB,
        COXVBN_CAP_REDLAB_MASK, COXVBN_CAP_REDLAB_DISCOVERY,
        &discovery));
    current_module = discovery;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_DISCOVERY,
        &request) == COXVBN_REDLAB_DENY_CAPABILITY);
    assert_audit(&log, COXVBN_REDLAB_DENY_CAPABILITY,
                 discovery.module_id, discovery.generation,
                 COXVBN_REDLAB_OP_DISCOVERY);

    assert(coxvbn_module_activate(&registry, &loader_authority, &vault,
                                  &discovery, 11U, key, 1U, proof, 1U));
    assert(coxvbn_module_authorize(
        &registry, &discovery, COXVBN_CAP_REDLAB_DISCOVERY));
    assert(!coxvbn_module_authorize(
        &registry, &discovery, COXVBN_CAP_REDLAB_PASSIVE_CAPTURE));
    assert(!coxvbn_module_authorize(
        &registry, &discovery, COXVBN_CAP_REDLAB_ACTIVE_PROBE));

    /* Discovery grant cannot be reused for passive or active operations. */
    request.job_id = 2U;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_DISCOVERY,
        &request) == COXVBN_REDLAB_ALLOW);
    assert_audit(&log, COXVBN_REDLAB_ALLOW, discovery.module_id,
                 discovery.generation, COXVBN_REDLAB_OP_DISCOVERY);
    assert(coxvbn_redlab_job_finish(&policy, request.job_id));

    request.job_id = 3U;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_PASSIVE_CAPTURE,
        &request) == COXVBN_REDLAB_DENY_CAPABILITY);
    assert_audit(&log, COXVBN_REDLAB_DENY_CAPABILITY, discovery.module_id,
                 discovery.generation, COXVBN_REDLAB_OP_PASSIVE_CAPTURE);

    request.job_id = 4U;
    request.active = true;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_ACTIVE_PROBE,
        &request) == COXVBN_REDLAB_DENY_CAPABILITY);
    assert_audit(&log, COXVBN_REDLAB_DENY_CAPABILITY, discovery.module_id,
                 discovery.generation, COXVBN_REDLAB_OP_ACTIVE_PROBE);
    request.active = false;

    /* The passive module has only the passive-capture grant. */
    activate_module(&registry, &loader_authority, &vault, COXVBN_DOMAIN_REDLAB,
                    COXVBN_CAP_REDLAB_PASSIVE_CAPTURE,
                    COXVBN_CAP_REDLAB_PASSIVE_CAPTURE, 12U, key, proof,
                    &passive);
    current_module = passive;
    request.job_id = 5U;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_PASSIVE_CAPTURE,
        &request) == COXVBN_REDLAB_ALLOW);
    assert_audit(&log, COXVBN_REDLAB_ALLOW, passive.module_id,
                 passive.generation, COXVBN_REDLAB_OP_PASSIVE_CAPTURE);
    assert(coxvbn_redlab_job_finish(&policy, request.job_id));
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_DISCOVERY,
        &request) == COXVBN_REDLAB_DENY_CAPABILITY);

    /* Active probing is a separate grant and requires active=true. */
    activate_module(&registry, &loader_authority, &vault, COXVBN_DOMAIN_REDLAB,
                    COXVBN_CAP_REDLAB_ACTIVE_PROBE,
                    COXVBN_CAP_REDLAB_ACTIVE_PROBE, 13U, key, proof,
                    &active);
    current_module = active;
    request.job_id = 6U;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_ACTIVE_PROBE,
        &request) == COXVBN_REDLAB_DENY_CAPABILITY);
    assert_audit(&log, COXVBN_REDLAB_DENY_CAPABILITY, active.module_id,
                 active.generation, COXVBN_REDLAB_OP_ACTIVE_PROBE);
    request.active = true;
    request.job_id = 7U;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_ACTIVE_PROBE,
        &request) == COXVBN_REDLAB_ALLOW);
    assert_audit(&log, COXVBN_REDLAB_ALLOW, active.module_id,
                 active.generation, COXVBN_REDLAB_OP_ACTIVE_PROBE);

    /* Invalid operation and active-flag mismatch are audited. */
    request.active = false;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_ACTIVE_PROBE,
        &request) == COXVBN_REDLAB_DENY_CAPABILITY);
    assert_audit(&log, COXVBN_REDLAB_DENY_CAPABILITY, active.module_id,
                 active.generation, COXVBN_REDLAB_OP_ACTIVE_PROBE);
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, invalid_operation,
        &request) == COXVBN_REDLAB_DENY_CAPABILITY);
    assert_audit(&log, COXVBN_REDLAB_DENY_CAPABILITY, active.module_id,
                 active.generation, 99U);

    /* A valid discovery grant reaches request validation and audits invalid input. */
    current_module = discovery;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_DISCOVERY,
        NULL) == COXVBN_REDLAB_DENY_INVALID);
    assert_audit(&log, COXVBN_REDLAB_DENY_INVALID, discovery.module_id,
                 discovery.generation, COXVBN_REDLAB_OP_DISCOVERY);
    request.target.family = 0U;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_DISCOVERY,
        &request) == COXVBN_REDLAB_DENY_INVALID);
    assert_audit(&log, COXVBN_REDLAB_DENY_INVALID, discovery.module_id,
                 discovery.generation, COXVBN_REDLAB_OP_DISCOVERY);
    request.target = target_address();

    /* Cross-domain registrations and authorizations are rejected. */
    assert(!coxvbn_module_register_domain(
        &registry, &loader_authority, COXVBN_DOMAIN_DAILY_PROFILE,
        COXVBN_CAP_REDLAB_DISCOVERY, COXVBN_CAP_REDLAB_DISCOVERY, &daily));
    assert(!coxvbn_module_register_domain(
        &registry, &loader_authority, COXVBN_DOMAIN_REDLAB,
        COXVBN_CAP_DAILY_PROFILE_DATA, COXVBN_CAP_DAILY_PROFILE_DATA, &daily));
    assert(!coxvbn_module_register_domain(
        &registry, &loader_authority, COXVBN_DOMAIN_REDLAB,
        COXVBN_CAP_REDLAB_DISCOVERY | COXVBN_CAP_DAILY_PROFILE_SETTINGS,
        COXVBN_CAP_REDLAB_DISCOVERY, &daily));
    activate_module(&registry, &loader_authority, &vault,
                    COXVBN_DOMAIN_DAILY_PROFILE,
                    COXVBN_CAP_DAILY_MASK, COXVBN_CAP_DAILY_PROFILE_DATA,
                    14U, key, proof, &daily);
    assert(!coxvbn_module_authorize(
        &registry, &daily, COXVBN_CAP_REDLAB_DISCOVERY));
    assert(!coxvbn_module_authorize(
        &registry, &active, COXVBN_CAP_DAILY_PROFILE_DATA));
    current_module = daily;
    request.job_id = 8U;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_DISCOVERY,
        &request) == COXVBN_REDLAB_DENY_CAPABILITY);
    assert_audit(&log, COXVBN_REDLAB_DENY_CAPABILITY, daily.module_id,
                 daily.generation, COXVBN_REDLAB_OP_DISCOVERY);

    /* Explicitly empty grants stay empty; registration never elevates by default. */
    activate_module(&registry, &loader_authority, &vault,
                    COXVBN_DOMAIN_REDLAB, COXVBN_CAP_REDLAB_MASK,
                    COXVBN_CAP_NONE, 15U, key, proof, &no_grant);
    current_module = no_grant;
    request.job_id = 9U;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_DISCOVERY,
        &request) == COXVBN_REDLAB_DENY_CAPABILITY);
    assert_audit(&log, COXVBN_REDLAB_DENY_CAPABILITY, no_grant.module_id,
                 no_grant.generation, COXVBN_REDLAB_OP_DISCOVERY);

    /* Revocation is terminal and the same admission path denies afterward. */
    assert(coxvbn_module_revoke(
        &registry, &loader_authority, &vault, &discovery));
    current_module = discovery;
    request.job_id = 10U;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_DISCOVERY,
        &request) == COXVBN_REDLAB_DENY_CAPABILITY);
    assert_audit(&log, COXVBN_REDLAB_DENY_CAPABILITY, discovery.module_id,
                 discovery.generation, COXVBN_REDLAB_OP_DISCOVERY);

    /* An unavailable provider or registry cannot supply trusted identity. */
    provider_available = false;
    request.job_id = 11U;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_DISCOVERY,
        &request) == COXVBN_REDLAB_DENY_CAPABILITY);
    assert_audit(&log, COXVBN_REDLAB_DENY_CAPABILITY, 0U, 0U,
                 COXVBN_REDLAB_OP_DISCOVERY);
    provider_available = true;
    current_module = active;
    assert(coxvbn_redlab_policy_check_current(
        &policy, NULL, COXVBN_REDLAB_OP_ACTIVE_PROBE,
        &request) == COXVBN_REDLAB_DENY_CAPABILITY);
    assert_audit(&log, COXVBN_REDLAB_DENY_CAPABILITY, 0U, 0U,
                 COXVBN_REDLAB_OP_ACTIVE_PROBE);

    /* Global stop is sticky and remains effective through the capability gate. */
    request.active = true;
    request.job_id = 12U;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_ACTIVE_PROBE,
        &request) == COXVBN_REDLAB_ALLOW);
    coxvbn_redlab_global_stop(&policy);
    request.job_id = 13U;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_ACTIVE_PROBE,
        &request) == COXVBN_REDLAB_DENY_STOPPED);
    assert_audit(&log, COXVBN_REDLAB_DENY_STOPPED, active.module_id,
                 active.generation, COXVBN_REDLAB_OP_ACTIVE_PROBE);
    request.job_id = 14U;
    assert(coxvbn_redlab_policy_check_current(
        &policy, &registry, COXVBN_REDLAB_OP_ACTIVE_PROBE,
        &request) == COXVBN_REDLAB_DENY_STOPPED);
    assert_audit(&log, COXVBN_REDLAB_DENY_STOPPED, active.module_id,
                 active.generation, COXVBN_REDLAB_OP_ACTIVE_PROBE);

    puts("COXVBN RedLab R2 single-entry admission tests: PASS");
    return 0;
}
