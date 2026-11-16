#include "module_registry.h"

static void registry_lock(struct coxvbn_module_registry *registry)
{
    while (atomic_exchange_explicit(&registry->locked, true,
                                    memory_order_acquire)) { }
}

static void registry_unlock(struct coxvbn_module_registry *registry)
{
    atomic_store_explicit(&registry->locked, false, memory_order_release);
}

static bool ref_equal(const struct coxvbn_module_ref *a,
                      const struct coxvbn_module_ref *b)
{
    return a->module_id == b->module_id &&
           a->generation == b->generation;
}

static struct coxvbn_module_record *find_record(
    struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref)
{
    size_t i;
    if (registry == NULL || ref == NULL) return NULL;
    for (i = 0U; i < COXVBN_MODULE_CAPACITY; ++i) {
        struct coxvbn_module_record *record = &registry->modules[i];
        if (record->state != COXVBN_MODULE_UNUSED &&
            ref_equal(&record->ref, ref))
            return record;
    }
    return NULL;
}

static const struct coxvbn_module_record *find_record_const(
    const struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref)
{
    size_t i;
    if (registry == NULL || ref == NULL) return NULL;
    for (i = 0U; i < COXVBN_MODULE_CAPACITY; ++i) {
        const struct coxvbn_module_record *record = &registry->modules[i];
        if (record->state != COXVBN_MODULE_UNUSED &&
            ref_equal(&record->ref, ref))
            return record;
    }
    return NULL;
}

void coxvbn_module_registry_init(struct coxvbn_module_registry *registry,
                                 const void *loader_authority)
{
    size_t i;
    if (registry == NULL) return;
    atomic_init(&registry->locked, false);
    registry->loader_authority = loader_authority;
    registry->next_module_id = 1U;
    for (i = 0U; i < COXVBN_MODULE_CAPACITY; ++i) {
        struct coxvbn_module_record *record = &registry->modules[i];
        record->ref.module_id = 0U;
        record->ref.generation = 0U;
        record->receiver_key.module_id = 0U;
        record->receiver_key.receiver_id = 0U;
        record->receiver_key.key_id = 0U;
        record->receiver_key.key_version = 0U;
        record->requested = COXVBN_CAP_NONE;
        record->granted = COXVBN_CAP_NONE;
        record->state = COXVBN_MODULE_UNUSED;
        record->domain = COXVBN_DOMAIN_SYSTEM;
    }
}

static bool module_register_domain_locked(
    struct coxvbn_module_registry *registry,
    const void *loader_authority,
    enum coxvbn_module_domain domain,
    coxvbn_capability_t requested,
    coxvbn_capability_t granted,
    struct coxvbn_module_ref *out)
{
    size_t i;
    if (registry == NULL || loader_authority == NULL ||
        loader_authority != registry->loader_authority || out == NULL ||
        registry->next_module_id == 0U ||
        (domain != COXVBN_DOMAIN_SYSTEM &&
         domain != COXVBN_DOMAIN_DAILY_PROFILE &&
         domain != COXVBN_DOMAIN_REDLAB) ||
        (domain == COXVBN_DOMAIN_DAILY_PROFILE &&
         ((requested & ~COXVBN_CAP_DAILY_MASK) != 0U ||
          (granted & ~COXVBN_CAP_DAILY_MASK) != 0U)) ||
        (domain == COXVBN_DOMAIN_REDLAB &&
         ((requested & ~COXVBN_CAP_REDLAB_MASK) != 0U ||
          (granted & ~COXVBN_CAP_REDLAB_MASK) != 0U)) ||
        (domain == COXVBN_DOMAIN_SYSTEM &&
         ((requested & ~COXVBN_CAP_SYSTEM_MASK) != 0U ||
          (granted & ~COXVBN_CAP_SYSTEM_MASK) != 0U)) ||
        (requested & ~COXVBN_CAP_ALL) != 0U ||
        (granted & ~requested) != 0U)
        return false;

    for (i = 0U; i < COXVBN_MODULE_CAPACITY; ++i) {
        struct coxvbn_module_record *record = &registry->modules[i];
        if (record->state == COXVBN_MODULE_UNUSED) {
            record->ref.module_id = registry->next_module_id++;
            record->ref.generation = 1U;
            record->receiver_key.module_id = 0U;
            record->receiver_key.receiver_id = 0U;
            record->receiver_key.key_id = 0U;
            record->receiver_key.key_version = 0U;
            record->requested = requested;
            record->granted = granted;
            record->domain = domain;
            record->state = COXVBN_MODULE_PENDING;
            *out = record->ref;
            return true;
        }
    }
    return false;
}

bool coxvbn_module_register(
    struct coxvbn_module_registry *registry,
    const void *loader_authority,
    coxvbn_capability_t requested,
    coxvbn_capability_t granted,
    struct coxvbn_module_ref *out)
{
    return coxvbn_module_register_domain(
        registry, loader_authority, COXVBN_DOMAIN_SYSTEM,
        requested, granted, out);
}

static bool module_begin_activation_locked(
    struct coxvbn_module_registry *registry,
    const void *loader_authority,
    const struct coxvbn_module_ref *ref,
    uint64_t *module_id_out)
{
    struct coxvbn_module_record *record;
    if (registry == NULL || loader_authority == NULL ||
        loader_authority != registry->loader_authority || ref == NULL ||
        module_id_out == NULL)
        return false;
    record = find_record(registry, ref);
    if (record == NULL || record->state != COXVBN_MODULE_PENDING)
        return false;
    record->state = COXVBN_MODULE_ACTIVATING;
    *module_id_out = record->ref.module_id;
    return true;
}

static bool module_finish_activation_locked(
    struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref,
    bool enrolled,
    const struct coxvbn_receiver_key_ref *key_ref)
{
    struct coxvbn_module_record *record = find_record(registry, ref);
    if (record == NULL || record->state != COXVBN_MODULE_ACTIVATING)
        return false;
    if (enrolled && key_ref != NULL) {
        record->receiver_key = *key_ref;
        record->state = COXVBN_MODULE_ACTIVE;
    } else {
        record->state = COXVBN_MODULE_PENDING;
    }
    return true;
}

static bool module_resolve_identity_locked(
    struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref,
    struct coxvbn_module_ref *canonical_out,
    enum coxvbn_module_domain *domain_out,
    enum coxvbn_module_state *state_out)
{
    const struct coxvbn_module_record *record = find_record_const(registry, ref);
    if (record == NULL) return false;
    if (canonical_out != NULL) *canonical_out = record->ref;
    if (domain_out != NULL) *domain_out = record->domain;
    if (state_out != NULL) *state_out = record->state;
    return true;
}

static bool module_is_active_locked(
    struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref)
{
    const struct coxvbn_module_record *record =
        find_record_const(registry, ref);
    return record != NULL && record->state == COXVBN_MODULE_ACTIVE;
}

static bool module_has_capability_locked(
    struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref,
    coxvbn_capability_t required)
{
    const struct coxvbn_module_record *record;
    if (required == COXVBN_CAP_NONE ||
        (required & ~COXVBN_CAP_ALL) != 0U)
        return false;
    record = find_record_const(registry, ref);
    if (record == NULL || record->state != COXVBN_MODULE_ACTIVE ||
        (record->granted & required) != required)
        return false;
    /* Re-check the domain at the authorization boundary as defense in depth.
     * Registration policy alone must never become the only isolation guard. */
    if ((required & COXVBN_CAP_REDLAB_MASK) != 0U &&
        (record->domain != COXVBN_DOMAIN_REDLAB ||
         (required & ~COXVBN_CAP_REDLAB_MASK) != 0U))
        return false;
    if ((required & COXVBN_CAP_DAILY_MASK) != 0U &&
        (record->domain != COXVBN_DOMAIN_DAILY_PROFILE ||
         (required & ~COXVBN_CAP_DAILY_MASK) != 0U))
        return false;
    return true;
}

bool coxvbn_module_authorize(
    struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref,
    coxvbn_capability_t required)
{
    return coxvbn_module_has_capability(registry, ref, required);
}

static bool module_begin_revoke_locked(
    struct coxvbn_module_registry *registry,
    const void *loader_authority,
    const struct coxvbn_module_ref *ref,
    struct coxvbn_receiver_key_ref *key_ref_out,
    bool *needs_vault_out)
{
    struct coxvbn_module_record *record;
    if (registry == NULL || loader_authority == NULL ||
        loader_authority != registry->loader_authority || ref == NULL ||
        key_ref_out == NULL || needs_vault_out == NULL)
        return false;
    record = find_record(registry, ref);
    if (record == NULL || (record->state != COXVBN_MODULE_PENDING &&
                           record->state != COXVBN_MODULE_ACTIVE))
        return false;
    *needs_vault_out = record->state == COXVBN_MODULE_ACTIVE;
    if (*needs_vault_out) {
        *key_ref_out = record->receiver_key;
        record->state = COXVBN_MODULE_REVOKING;
    } else {
        record->state = COXVBN_MODULE_REVOKED;
        record->granted = COXVBN_CAP_NONE;
    }
    return true;
}

static bool module_finish_revoke_locked(
    struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref,
    bool revoked)
{
    struct coxvbn_module_record *record = find_record(registry, ref);
    if (record == NULL || record->state != COXVBN_MODULE_REVOKING)
        return false;
    if (revoked) {
        record->state = COXVBN_MODULE_REVOKED;
        record->granted = COXVBN_CAP_NONE;
    } else {
        record->state = COXVBN_MODULE_ACTIVE;
    }
    return true;
}



bool coxvbn_module_register_domain(
    struct coxvbn_module_registry *registry,
    const void *loader_authority, enum coxvbn_module_domain domain,
    coxvbn_capability_t requested, coxvbn_capability_t granted,
    struct coxvbn_module_ref *out)
{
    bool result;
    if (registry == NULL) return false;
    registry_lock(registry);
    result = module_register_domain_locked(registry, loader_authority,
        domain, requested, granted, out);
    registry_unlock(registry);
    return result;
}

bool coxvbn_module_activate(
    struct coxvbn_module_registry *registry, const void *loader_authority,
    struct coxvbn_key_vault *vault, const struct coxvbn_module_ref *ref,
    uint64_t receiver_id, const uint8_t *public_key, size_t public_key_len,
    const uint8_t *proof, size_t proof_len)
{
    bool begun, enrolled, finalized;
    uint64_t module_id = 0U;
    struct coxvbn_receiver_key_ref key_ref;
    if (registry == NULL || vault == NULL || receiver_id == 0U)
        return false;
    registry_lock(registry);
    begun = module_begin_activation_locked(
        registry, loader_authority, ref, &module_id);
    registry_unlock(registry);
    if (!begun) return false;
    enrolled = coxvbn_key_vault_enroll_receiver(
        vault, module_id, receiver_id, public_key, public_key_len,
        proof, proof_len, &key_ref);
    registry_lock(registry);
    finalized = module_finish_activation_locked(
        registry, ref, enrolled, enrolled ? &key_ref : NULL);
    registry_unlock(registry);
    if (enrolled && !finalized) {
        (void)coxvbn_key_vault_revoke_receiver(vault, module_id, &key_ref);
        return false;
    }
    return enrolled && finalized;
}

bool coxvbn_module_resolve_identity(
    struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref,
    struct coxvbn_module_ref *canonical_out,
    enum coxvbn_module_domain *domain_out,
    enum coxvbn_module_state *state_out)
{
    bool result;
    if (registry == NULL) return false;
    registry_lock(registry);
    result = module_resolve_identity_locked(registry, ref, canonical_out,
        domain_out, state_out);
    registry_unlock(registry);
    return result;
}

bool coxvbn_module_is_active(
    struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref)
{
    bool result;
    if (registry == NULL) return false;
    registry_lock(registry);
    result = module_is_active_locked(registry, ref);
    registry_unlock(registry);
    return result;
}

bool coxvbn_module_has_capability(
    struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref, coxvbn_capability_t required)
{
    bool result;
    if (registry == NULL) return false;
    registry_lock(registry);
    result = module_has_capability_locked(registry, ref, required);
    registry_unlock(registry);
    return result;
}

bool coxvbn_module_revoke(
    struct coxvbn_module_registry *registry, const void *loader_authority,
    struct coxvbn_key_vault *vault, const struct coxvbn_module_ref *ref)
{
    bool begun, needs_vault, revoked = true, finalized;
    struct coxvbn_receiver_key_ref key_ref;
    if (registry == NULL) return false;
    registry_lock(registry);
    begun = module_begin_revoke_locked(
        registry, loader_authority, ref, &key_ref, &needs_vault);
    registry_unlock(registry);
    if (!begun) return false;
    if (!needs_vault) return true;
    if (vault == NULL) revoked = false;
    else revoked = coxvbn_key_vault_revoke_receiver(
        vault, key_ref.module_id, &key_ref);
    registry_lock(registry);
    finalized = module_finish_revoke_locked(registry, ref, revoked);
    registry_unlock(registry);
    return finalized && revoked;
}
