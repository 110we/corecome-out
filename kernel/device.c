#include "device.h"

static size_t device_length(const char *s, size_t limit)
{
    size_t n = 0U;
    while (n < limit && s[n] != '\0') ++n;
    return n;
}

static int device_equal(const char *a, const char *b)
{
    size_t i = 0U;
    while (a[i] != '\0' && a[i] == b[i]) ++i;
    return a[i] == b[i];
}

static void device_copy(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0U; i < n; ++i) dst[i] = src[i];
}

static void device_zero(void *p, size_t n)
{
    uint8_t *b = (uint8_t *)p;
    size_t i;
    for (i = 0U; i < n; ++i) b[i] = 0U;
}

static struct coxvbn_device_record *device_find(
    struct coxvbn_device_registry *registry,
    const struct coxvbn_device_ref *ref)
{
    uint32_t i;
    if (registry == 0 || ref == 0 || ref->id == 0U ||
        ref->generation == 0U) return 0;
    for (i = 0U; i < COXVBN_DEVICE_CAPACITY; ++i) {
        struct coxvbn_device_record *record = &registry->devices[i];
        if (record->state != COXVBN_DEVICE_UNUSED &&
            record->ref.id == ref->id &&
            record->ref.generation == ref->generation)
            return record;
    }
    return 0;
}

static const struct coxvbn_device_record *device_find_const(
    const struct coxvbn_device_registry *registry,
    const struct coxvbn_device_ref *ref)
{
    uint32_t i;
    if (registry == 0 || ref == 0 || ref->id == 0U ||
        ref->generation == 0U) return 0;
    for (i = 0U; i < COXVBN_DEVICE_CAPACITY; ++i) {
        const struct coxvbn_device_record *record = &registry->devices[i];
        if (record->state != COXVBN_DEVICE_UNUSED &&
            record->ref.id == ref->id &&
            record->ref.generation == ref->generation)
            return record;
    }
    return 0;
}

void coxvbn_device_registry_init(struct coxvbn_device_registry *registry)
{
    if (registry == 0) return;
    device_zero(registry, sizeof(*registry));
    registry->next_id = 1U;
    registry->generation = 1U;
}

int coxvbn_device_register(struct coxvbn_device_registry *registry,
                           const char *name,
                           enum coxvbn_device_class device_class,
                           const struct coxvbn_device_ops *ops,
                           void *context,
                           struct coxvbn_device_ref *ref_out)
{
    uint32_t i;
    size_t name_len;
    struct coxvbn_device_record *slot = 0;

    if (registry == 0 || name == 0 || ops == 0 || ops->probe == 0 ||
        ops->remove == 0 || ops->suspend == 0 || ops->resume == 0 ||
        ref_out == 0 ||
        device_class < COXVBN_DEVICE_CLASS_PLATFORM ||
        device_class > COXVBN_DEVICE_CLASS_CONSOLE)
        return -1;
    name_len = device_length(name, COXVBN_DEVICE_NAME_MAX + 1U);
    if (name_len == 0U || name_len > COXVBN_DEVICE_NAME_MAX) return -1;
    if (registry->next_id == 0U || registry->generation == 0U) return -2;

    for (i = 0U; i < COXVBN_DEVICE_CAPACITY; ++i) {
        struct coxvbn_device_record *record = &registry->devices[i];
        if (record->state != COXVBN_DEVICE_UNUSED &&
            device_equal(record->name, name))
            return -3;
        if (record->state == COXVBN_DEVICE_UNUSED && slot == 0)
            slot = record;
    }
    if (slot == 0) return -4;

    device_zero(slot, sizeof(*slot));
    device_copy(slot->name, name, name_len + 1U);
    slot->ref.id = registry->next_id++;
    slot->ref.generation = registry->generation++;
    if (registry->next_id == 0U || registry->generation == 0U) {
        device_zero(slot, sizeof(*slot));
        return -2;
    }
    slot->ops = ops;
    slot->context = context;
    slot->device_class = device_class;
    slot->state = COXVBN_DEVICE_SUSPENDED; /* Not visible as active until probe. */
    {
        int rc = ops->probe(context);
        if (rc != 0) {
            device_zero(slot, sizeof(*slot));
            return rc;
        }
    }
    slot->state = COXVBN_DEVICE_ACTIVE;
    slot->last_error = 0;
    *ref_out = slot->ref;
    ++registry->count;
    return 0;
}

int coxvbn_device_unregister(struct coxvbn_device_registry *registry,
                             const struct coxvbn_device_ref *ref)
{
    struct coxvbn_device_record *record = device_find(registry, ref);
    int rc;
    if (record == 0) return -1;
    rc = record->ops->remove(record->context);
    if (rc != 0) {
        record->last_error = rc;
        return rc;
    }
    device_zero(record, sizeof(*record));
    if (registry->count != 0U) --registry->count;
    return 0;
}

int coxvbn_device_suspend(struct coxvbn_device_registry *registry,
                          const struct coxvbn_device_ref *ref)
{
    struct coxvbn_device_record *record = device_find(registry, ref);
    int rc;
    if (record == 0 || record->state != COXVBN_DEVICE_ACTIVE) return -1;
    rc = record->ops->suspend(record->context);
    if (rc != 0) {
        record->last_error = rc;
        return rc;
    }
    record->state = COXVBN_DEVICE_SUSPENDED;
    record->last_error = 0;
    return 0;
}

int coxvbn_device_resume(struct coxvbn_device_registry *registry,
                         const struct coxvbn_device_ref *ref)
{
    struct coxvbn_device_record *record = device_find(registry, ref);
    int rc;
    if (record == 0 || record->state != COXVBN_DEVICE_SUSPENDED) return -1;
    rc = record->ops->resume(record->context);
    if (rc != 0) {
        record->last_error = rc;
        return rc;
    }
    record->state = COXVBN_DEVICE_ACTIVE;
    record->last_error = 0;
    return 0;
}

int coxvbn_device_lookup(const struct coxvbn_device_registry *registry,
                         const char *name,
                         struct coxvbn_device_ref *ref_out)
{
    uint32_t i;
    if (registry == 0 || name == 0 || ref_out == 0 ||
        device_length(name, COXVBN_DEVICE_NAME_MAX + 1U) == 0U ||
        device_length(name, COXVBN_DEVICE_NAME_MAX + 1U) >
            COXVBN_DEVICE_NAME_MAX)
        return -1;
    for (i = 0U; i < COXVBN_DEVICE_CAPACITY; ++i) {
        const struct coxvbn_device_record *record = &registry->devices[i];
        if (record->state != COXVBN_DEVICE_UNUSED &&
            device_equal(record->name, name)) {
            *ref_out = record->ref;
            return 0;
        }
    }
    return -2;
}

int coxvbn_device_get_state(const struct coxvbn_device_registry *registry,
                            const struct coxvbn_device_ref *ref,
                            enum coxvbn_device_state *state_out,
                            int *last_error_out)
{
    const struct coxvbn_device_record *record =
        device_find_const(registry, ref);
    if (record == 0 || state_out == 0) return -1;
    *state_out = record->state;
    if (last_error_out != 0) *last_error_out = record->last_error;
    return 0;
}
