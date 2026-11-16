#include <assert.h>
#include <stdio.h>
#include "privacy_profile.h"

struct test_context {
    bool authorize;
    bool stop_and_revoke;
    bool audit_ok;
    unsigned int denied;
    unsigned int entered;
    unsigned int returned;
    unsigned int failed;
};

static bool authorize_redlab(void *context)
{
    return ((struct test_context *)context)->authorize;
}

static bool stop_and_revoke(void *context)
{
    return ((struct test_context *)context)->stop_and_revoke;
}

static bool audit_event(enum coxvbn_profile_audit_event event,
                        enum coxvbn_profile_state from,
                        enum coxvbn_profile_state to, void *context)
{
    struct test_context *test = context;
    (void)from;
    (void)to;
    if (event == COXVBN_PROFILE_AUDIT_SWITCH_DENIED) ++test->denied;
    if (event == COXVBN_PROFILE_AUDIT_ENTER_REDLAB) ++test->entered;
    if (event == COXVBN_PROFILE_AUDIT_RETURN_DAILY) ++test->returned;
    if (event == COXVBN_PROFILE_AUDIT_FAIL_CLOSED) ++test->failed;
    return test->audit_ok;
}

static struct coxvbn_profile_ops make_ops(struct test_context *context)
{
    struct coxvbn_profile_ops ops = {
        authorize_redlab, stop_and_revoke, audit_event, context
    };
    return ops;
}

int main(void)
{
    struct test_context context = {true, true, true, 0U, 0U, 0U, 0U};
    struct coxvbn_profile_ops ops = make_ops(&context);
    struct coxvbn_profile_manager manager;
    struct coxvbn_profile_handle daily, redlab;

    assert(coxvbn_profile_manager_init(&manager, 77U, &ops));
    assert(coxvbn_profile_get_handle(&manager, &daily));
    assert(coxvbn_profile_handle_valid(
        &manager, &daily, COXVBN_PROFILE_DOMAIN_DAILY));
    assert(!coxvbn_profile_handle_valid(
        &manager, &daily, COXVBN_PROFILE_DOMAIN_REDLAB));

    assert(!coxvbn_profile_switch_to_redlab(&manager, false));
    assert(manager.state == COXVBN_PROFILE_DAILY_ACTIVE);
    assert(context.denied == 1U);

    context.authorize = false;
    assert(!coxvbn_profile_switch_to_redlab(&manager, true));
    assert(manager.state == COXVBN_PROFILE_DAILY_ACTIVE);
    context.authorize = true;

    assert(coxvbn_profile_switch_to_redlab(&manager, true));
    assert(manager.state == COXVBN_PROFILE_REDLAB_ACTIVE);
    assert(!coxvbn_profile_handle_valid(
        &manager, &daily, COXVBN_PROFILE_DOMAIN_DAILY));
    assert(coxvbn_profile_get_handle(&manager, &redlab));
    assert(coxvbn_profile_handle_valid(
        &manager, &redlab, COXVBN_PROFILE_DOMAIN_REDLAB));
    assert(!coxvbn_profile_handle_valid(
        &manager, &redlab, COXVBN_PROFILE_DOMAIN_DAILY));
    assert(context.entered == 1U);

    context.stop_and_revoke = false;
    assert(!coxvbn_profile_switch_to_daily(&manager));
    assert(manager.state == COXVBN_PROFILE_RESTRICTED);
    assert(!coxvbn_profile_get_handle(&manager, &redlab));
    assert(context.failed == 1U);
    assert(!coxvbn_profile_switch_to_redlab(&manager, true));

    context.stop_and_revoke = true;
    context.audit_ok = true;
    assert(coxvbn_profile_manager_init(&manager, 78U, &ops));
    assert(coxvbn_profile_switch_to_redlab(&manager, true));
    context.audit_ok = false;
    assert(!coxvbn_profile_switch_to_daily(&manager));
    assert(manager.state == COXVBN_PROFILE_RESTRICTED);

    context.audit_ok = true;
    assert(coxvbn_profile_manager_init(&manager, 79U, &ops));
    assert(coxvbn_profile_switch_to_redlab(&manager, true));
    assert(coxvbn_profile_switch_to_daily(&manager));
    assert(manager.state == COXVBN_PROFILE_DAILY_ACTIVE);
    assert(coxvbn_profile_get_handle(&manager, &daily));
    assert(coxvbn_profile_handle_valid(
        &manager, &daily, COXVBN_PROFILE_DOMAIN_DAILY));
    assert(context.returned == 2U);

    puts("COXVBN privacy profile transition tests: PASS");
    return 0;
}
