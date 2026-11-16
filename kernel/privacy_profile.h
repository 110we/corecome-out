#ifndef COXVBN_PRIVACY_PROFILE_H
#define COXVBN_PRIVACY_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

enum coxvbn_profile_domain {
    COXVBN_PROFILE_DOMAIN_DAILY = 1,
    COXVBN_PROFILE_DOMAIN_REDLAB = 2
};

enum coxvbn_profile_state {
    COXVBN_PROFILE_DAILY_ACTIVE = 0,
    COXVBN_PROFILE_SWITCHING_TO_REDLAB = 1,
    COXVBN_PROFILE_REDLAB_ACTIVE = 2,
    COXVBN_PROFILE_SWITCHING_TO_DAILY = 3,
    COXVBN_PROFILE_RESTRICTED = 4
};

enum coxvbn_profile_audit_event {
    COXVBN_PROFILE_AUDIT_SWITCH_DENIED = 0,
    COXVBN_PROFILE_AUDIT_ENTER_REDLAB = 1,
    COXVBN_PROFILE_AUDIT_RETURN_DAILY = 2,
    COXVBN_PROFILE_AUDIT_FAIL_CLOSED = 3
};

struct coxvbn_profile_handle {
    uint64_t profile_id;
    uint64_t generation;
    enum coxvbn_profile_domain domain;
};

/*
 * These callbacks are kernel-internal contracts. They must be bound to trusted
 * authentication, RedLab scope authorization, job cancellation/revocation,
 * and durable audit implementations before production use.
 */
struct coxvbn_profile_ops {
    bool (*authorize_redlab)(void *context);
    bool (*stop_and_revoke_redlab)(void *context);
    bool (*audit)(enum coxvbn_profile_audit_event event,
                  enum coxvbn_profile_state from,
                  enum coxvbn_profile_state to, void *context);
    void *context;
};

struct coxvbn_profile_manager {
    uint64_t profile_id;
    uint64_t generation;
    enum coxvbn_profile_state state;
    struct coxvbn_profile_ops ops;
};

bool coxvbn_profile_manager_init(
    struct coxvbn_profile_manager *manager, uint64_t profile_id,
    const struct coxvbn_profile_ops *ops);

bool coxvbn_profile_switch_to_redlab(
    struct coxvbn_profile_manager *manager, bool explicit_confirmation);

bool coxvbn_profile_switch_to_daily(
    struct coxvbn_profile_manager *manager);

bool coxvbn_profile_get_handle(
    const struct coxvbn_profile_manager *manager,
    struct coxvbn_profile_handle *out);

/* Kernel-side validation only; handles must never be user-forgeable. */
bool coxvbn_profile_handle_valid(
    const struct coxvbn_profile_manager *manager,
    const struct coxvbn_profile_handle *handle,
    enum coxvbn_profile_domain required_domain);

#endif
