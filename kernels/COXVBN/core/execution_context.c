#include "execution_context.h"
#include <stdatomic.h>

static atomic_uint provider_state = ATOMIC_VAR_INIT(0U);
static coxvbn_current_module_provider current_provider;
static void *provider_context;

bool coxvbn_execution_context_install_provider(
    coxvbn_current_module_provider provider, void *context)
{
    unsigned int expected = 0U;
    if (provider == 0)
        return false;
    if (!atomic_compare_exchange_strong_explicit(
            &provider_state, &expected, 1U,
            memory_order_acq_rel, memory_order_acquire))
        return false;

    current_provider = provider;
    provider_context = context;
    atomic_store_explicit(&provider_state, 2U, memory_order_release);
    return true;
}

bool coxvbn_execution_context_current_module(
    struct coxvbn_module_ref *module_out)
{
    if (module_out == 0)
        return false;
    module_out->module_id = 0U;
    module_out->generation = 0U;
    if (atomic_load_explicit(&provider_state, memory_order_acquire) != 2U)
        return false;
    if (!current_provider(module_out, provider_context)) {
        module_out->module_id = 0U;
        module_out->generation = 0U;
        return false;
    }
    if (module_out->module_id == 0U || module_out->generation == 0U) {
        module_out->module_id = 0U;
        module_out->generation = 0U;
        return false;
    }
    return true;
}
