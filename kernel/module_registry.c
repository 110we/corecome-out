#include "module_registry.h"

static bool ref_equal(const struct coxvbn_module_ref *a,
                      const struct coxvbn_module_ref *b)
{
    return a->module_id == b->module_id &&
           a->generation == b->generation;
}

static const struct coxvbn_module_record *find_record(
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
    registry->loader_authority = loader_authority;
    registry->next_module_id = 1U;
    for (i = 0U; i < COXVBN_MODULE_CAPACITY; ++i) {
        registry->modules[i].ref.module_id = 0U;
        registry->modules[i].ref.generation = 0U;
        registry->modules[i].requested = COXVBN_CAP_NONE;
        registry->modules[i].granted = COXVBN_CAP_NONE;
        registry->modules[i].state = COXVBN_MODULE_UNUSED;
    }
}

bool coxvbn_module_register(
    struct coxvbn_module_registry *registry,
    const void *loader_authority,
    coxvbn_capability_t requested,
    coxvbn_capability_t granted,
    struct coxvbn_module_ref *out)
{
    size_t i;
    if (registry == NULL || loader_authority == NULL ||
        loader_authority != registry->loader_authority || out == NULL ||
        registry->next_module_id == 0U ||
        (requested & ~COXVBN_CAP_ALL) != 0U ||
        (granted & ~requested) != 0U)
        return false;

    for (i = 0U; i < COXVBN_MODULE_CAPACITY; ++i) {
        struct coxvbn_module_record *record = &registry->modules[i];
        if (record->state == COXVBN_MODULE_UNUSED) {
            record->ref.module_id = registry->next_module_id++;
            record->ref.generation = 1U;
            record->requested = requested;
            record->granted = granted;
            record->state = COXVBN_MODULE_ACTIVE;
            *out = record->ref;
            return true;
        }
    }
    return false;
}

bool coxvbn_module_is_active(
    const struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref)
{
    const struct coxvbn_module_record *record = find_record(registry, ref);
    return record != NULL && record->state == COXVBN_MODULE_ACTIVE;
}

bool coxvbn_module_has_capability(
    const struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref,
    coxvbn_capability_t required)
{
    const struct coxvbn_module_record *record;
    if (required == COXVBN_CAP_NONE ||
        (required & ~COXVBN_CAP_ALL) != 0U)
        return false;
    record = find_record(registry, ref);
    return record != NULL && record->state == COXVBN_MODULE_ACTIVE &&
           (record->granted & required) == required;
}

bool coxvbn_module_authorize(
    const struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref,
    coxvbn_capability_t required)
{
    return coxvbn_module_has_capability(registry, ref, required);
}

bool coxvbn_module_revoke(
    struct coxvbn_module_registry *registry,
    const void *loader_authority,
    const struct coxvbn_module_ref *ref)
{
    size_t i;
    if (registry == NULL || loader_authority == NULL ||
        loader_authority != registry->loader_authority || ref == NULL)
        return false;
    for (i = 0U; i < COXVBN_MODULE_CAPACITY; ++i) {
        struct coxvbn_module_record *record = &registry->modules[i];
        if (record->state == COXVBN_MODULE_ACTIVE &&
            ref_equal(&record->ref, ref)) {
            record->state = COXVBN_MODULE_REVOKED;
            record->granted = COXVBN_CAP_NONE;
            return true;
        }
    }
    return false;
}
