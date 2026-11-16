#include "privacy_profile.h"

static void fail_closed(struct coxvbn_profile_manager *manager,
                        enum coxvbn_profile_state from)
{
    manager->state = COXVBN_PROFILE_RESTRICTED;
    (void)manager->ops.audit(COXVBN_PROFILE_AUDIT_FAIL_CLOSED, from,
                             COXVBN_PROFILE_RESTRICTED,
                             manager->ops.context);
}

bool coxvbn_profile_manager_init(
    struct coxvbn_profile_manager *manager, uint64_t profile_id,
    const struct coxvbn_profile_ops *ops)
{
    if (manager == NULL || profile_id == 0U || ops == NULL ||
        ops->authorize_redlab == NULL ||
        ops->stop_and_revoke_redlab == NULL || ops->audit == NULL)
        return false;

    manager->profile_id = profile_id;
    manager->generation = 1U;
    manager->state = COXVBN_PROFILE_DAILY_ACTIVE;
    manager->ops = *ops;
    return true;
}

bool coxvbn_profile_switch_to_redlab(
    struct coxvbn_profile_manager *manager, bool explicit_confirmation)
{
    enum coxvbn_profile_state from = COXVBN_PROFILE_DAILY_ACTIVE;

    if (manager == NULL || manager->state != from)
        return false;

    manager->state = COXVBN_PROFILE_SWITCHING_TO_REDLAB;
    if (!explicit_confirmation || !manager->ops.authorize_redlab(
            manager->ops.context)) {
        manager->state = from;
        if (!manager->ops.audit(COXVBN_PROFILE_AUDIT_SWITCH_DENIED, from,
                                from, manager->ops.context))
            fail_closed(manager, from);
        return false;
    }

    if (manager->generation == UINT64_MAX ||
        !manager->ops.audit(COXVBN_PROFILE_AUDIT_ENTER_REDLAB, from,
                            COXVBN_PROFILE_REDLAB_ACTIVE,
                            manager->ops.context)) {
        fail_closed(manager, from);
        return false;
    }

    ++manager->generation;
    manager->state = COXVBN_PROFILE_REDLAB_ACTIVE;
    return true;
}

bool coxvbn_profile_switch_to_daily(
    struct coxvbn_profile_manager *manager)
{
    enum coxvbn_profile_state from = COXVBN_PROFILE_REDLAB_ACTIVE;

    if (manager == NULL || manager->state != from)
        return false;

    manager->state = COXVBN_PROFILE_SWITCHING_TO_DAILY;
    if (!manager->ops.stop_and_revoke_redlab(manager->ops.context)) {
        fail_closed(manager, from);
        return false;
    }

    if (manager->generation == UINT64_MAX ||
        !manager->ops.audit(COXVBN_PROFILE_AUDIT_RETURN_DAILY, from,
                            COXVBN_PROFILE_DAILY_ACTIVE,
                            manager->ops.context)) {
        fail_closed(manager, from);
        return false;
    }

    ++manager->generation;
    manager->state = COXVBN_PROFILE_DAILY_ACTIVE;
    return true;
}

bool coxvbn_profile_get_handle(
    const struct coxvbn_profile_manager *manager,
    struct coxvbn_profile_handle *out)
{
    if (manager == NULL || out == NULL)
        return false;

    out->profile_id = manager->profile_id;
    out->generation = manager->generation;
    if (manager->state == COXVBN_PROFILE_DAILY_ACTIVE)
        out->domain = COXVBN_PROFILE_DOMAIN_DAILY;
    else if (manager->state == COXVBN_PROFILE_REDLAB_ACTIVE)
        out->domain = COXVBN_PROFILE_DOMAIN_REDLAB;
    else
        return false;
    return true;
}

bool coxvbn_profile_handle_valid(
    const struct coxvbn_profile_manager *manager,
    const struct coxvbn_profile_handle *handle,
    enum coxvbn_profile_domain required_domain)
{
    enum coxvbn_profile_domain active_domain;

    if (manager == NULL || handle == NULL ||
        (manager->state != COXVBN_PROFILE_DAILY_ACTIVE &&
         manager->state != COXVBN_PROFILE_REDLAB_ACTIVE))
        return false;

    active_domain = manager->state == COXVBN_PROFILE_DAILY_ACTIVE
        ? COXVBN_PROFILE_DOMAIN_DAILY : COXVBN_PROFILE_DOMAIN_REDLAB;

    return handle->profile_id == manager->profile_id &&
           handle->generation == manager->generation &&
           handle->domain == active_domain &&
           required_domain == active_domain;
}
