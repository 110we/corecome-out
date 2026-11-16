#include "key_vault.h"

static void copy_bytes(uint8_t *dst, const uint8_t *src, size_t len)
{
    size_t i;
    for (i = 0U; i < len; ++i) dst[i] = src[i];
}

static bool valid_key_input(const uint8_t *key, size_t key_len,
                            const uint8_t *proof, size_t proof_len)
{
    return key != NULL && key_len != 0U &&
           key_len <= COXVBN_KEY_MAX_PUBLIC_BYTES &&
           proof != NULL && proof_len != 0U &&
           proof_len <= COXVBN_KEY_MAX_PROOF_BYTES;
}

static bool refs_equal(const struct coxvbn_receiver_key_ref *a,
                       const struct coxvbn_receiver_key_ref *b)
{
    return a->module_id == b->module_id &&
           a->receiver_id == b->receiver_id &&
           a->key_id == b->key_id &&
           a->key_version == b->key_version;
}

static struct coxvbn_receiver_key_record *find_record(
    struct coxvbn_key_vault *vault,
    const struct coxvbn_receiver_key_ref *ref)
{
    size_t i;
    for (i = 0U; i < COXVBN_RECEIVER_KEY_CAPACITY; ++i) {
        struct coxvbn_receiver_key_record *record = &vault->receiver_keys[i];
        if (record->state != COXVBN_RECEIVER_KEY_UNUSED &&
            refs_equal(&record->ref, ref))
            return record;
    }
    return NULL;
}

static struct coxvbn_receiver_key_record *free_record(
    struct coxvbn_key_vault *vault)
{
    size_t i;
    for (i = 0U; i < COXVBN_RECEIVER_KEY_CAPACITY; ++i) {
        if (vault->receiver_keys[i].state == COXVBN_RECEIVER_KEY_UNUSED)
            return &vault->receiver_keys[i];
    }
    return NULL;
}

void coxvbn_key_vault_init(struct coxvbn_key_vault *vault,
                           coxvbn_key_verify_fn verify, void *context)
{
    size_t i;
    if (vault == NULL) return;
    vault->verify = verify;
    vault->context = context;
    vault->initialized = (verify != NULL);
    vault->receiver_verify = NULL;
    vault->receiver_context = NULL;
    vault->next_receiver_key_id = 1U;
    for (i = 0U; i < COXVBN_RECEIVER_KEY_CAPACITY; ++i) {
        vault->receiver_keys[i].ref.module_id = 0U;
        vault->receiver_keys[i].ref.receiver_id = 0U;
        vault->receiver_keys[i].ref.key_id = 0U;
        vault->receiver_keys[i].ref.key_version = 0U;
        vault->receiver_keys[i].public_key_len = 0U;
        vault->receiver_keys[i].state = COXVBN_RECEIVER_KEY_UNUSED;
    }
}

bool coxvbn_key_vault_verify(const struct coxvbn_key_vault *vault,
                             const uint8_t *proof, size_t proof_len)
{
    if (vault == NULL || !vault->initialized || vault->verify == NULL ||
        proof == NULL || proof_len == 0U)
        return false;
    return vault->verify(proof, proof_len, vault->context);
}

void coxvbn_key_vault_set_receiver_verifier(
    struct coxvbn_key_vault *vault, coxvbn_receiver_proof_fn verify,
    void *context)
{
    if (vault == NULL) return;
    vault->receiver_verify = verify;
    vault->receiver_context = context;
}

bool coxvbn_key_vault_enroll_receiver(
    struct coxvbn_key_vault *vault, uint64_t caller_module_id,
    uint64_t receiver_id, const uint8_t *public_key, size_t public_key_len,
    const uint8_t *proof, size_t proof_len,
    struct coxvbn_receiver_key_ref *out)
{
    struct coxvbn_receiver_key_record *record;
    if (vault == NULL || !vault->initialized ||
        vault->receiver_verify == NULL || caller_module_id == 0U ||
        receiver_id == 0U || out == NULL ||
        !valid_key_input(public_key, public_key_len, proof, proof_len) ||
        vault->next_receiver_key_id == 0U)
        return false;
    record = free_record(vault);
    if (record == NULL ||
        !vault->receiver_verify(public_key, public_key_len, proof, proof_len,
                                vault->receiver_context))
        return false;

    record->ref.module_id = caller_module_id;
    record->ref.receiver_id = receiver_id;
    record->ref.key_id = vault->next_receiver_key_id++;
    record->ref.key_version = 1U;
    copy_bytes(record->public_key, public_key, public_key_len);
    record->public_key_len = public_key_len;
    record->state = COXVBN_RECEIVER_KEY_ACTIVE;
    *out = record->ref;
    return true;
}

bool coxvbn_key_vault_rotate_receiver(
    struct coxvbn_key_vault *vault, uint64_t caller_module_id,
    const struct coxvbn_receiver_key_ref *old_ref,
    const uint8_t *new_public_key, size_t new_public_key_len,
    const uint8_t *proof, size_t proof_len,
    struct coxvbn_receiver_key_ref *out)
{
    struct coxvbn_receiver_key_record *old_record;
    struct coxvbn_receiver_key_record *new_record;
    if (vault == NULL || !vault->initialized ||
        vault->receiver_verify == NULL || old_ref == NULL || out == NULL ||
        caller_module_id == 0U || caller_module_id != old_ref->module_id ||
        old_ref->key_version == UINT64_MAX ||
        vault->next_receiver_key_id == 0U ||
        !valid_key_input(new_public_key, new_public_key_len, proof, proof_len))
        return false;

    old_record = find_record(vault, old_ref);
    new_record = free_record(vault);
    if (old_record == NULL || old_record->state != COXVBN_RECEIVER_KEY_ACTIVE ||
        new_record == NULL ||
        !vault->receiver_verify(new_public_key, new_public_key_len,
                                proof, proof_len, vault->receiver_context))
        return false;

    /* All failure points precede mutation: publish new version and revoke old
     * in one bounded in-memory operation. Revoked records are never reused. */
    new_record->ref.module_id = old_ref->module_id;
    new_record->ref.receiver_id = old_ref->receiver_id;
    new_record->ref.key_id = vault->next_receiver_key_id++;
    new_record->ref.key_version = old_ref->key_version + 1U;
    copy_bytes(new_record->public_key, new_public_key, new_public_key_len);
    new_record->public_key_len = new_public_key_len;
    new_record->state = COXVBN_RECEIVER_KEY_ACTIVE;
    old_record->state = COXVBN_RECEIVER_KEY_REVOKED;
    *out = new_record->ref;
    return true;
}

bool coxvbn_key_vault_revoke_receiver(
    struct coxvbn_key_vault *vault, uint64_t caller_module_id,
    const struct coxvbn_receiver_key_ref *ref)
{
    struct coxvbn_receiver_key_record *record;
    if (vault == NULL || !vault->initialized || ref == NULL ||
        caller_module_id == 0U || caller_module_id != ref->module_id)
        return false;
    record = find_record(vault, ref);
    if (record == NULL || record->state != COXVBN_RECEIVER_KEY_ACTIVE)
        return false;
    record->state = COXVBN_RECEIVER_KEY_REVOKED;
    return true;
}

bool coxvbn_key_vault_verify_receiver(
    const struct coxvbn_key_vault *vault,
    const struct coxvbn_receiver_key_ref *ref,
    const uint8_t *proof, size_t proof_len)
{
    size_t i;
    if (vault == NULL || !vault->initialized ||
        vault->receiver_verify == NULL || ref == NULL ||
        proof == NULL || proof_len == 0U ||
        proof_len > COXVBN_KEY_MAX_PROOF_BYTES)
        return false;
    for (i = 0U; i < COXVBN_RECEIVER_KEY_CAPACITY; ++i) {
        const struct coxvbn_receiver_key_record *record =
            &vault->receiver_keys[i];
        if (record->state == COXVBN_RECEIVER_KEY_ACTIVE &&
            refs_equal(&record->ref, ref))
            return vault->receiver_verify(record->public_key,
                                          record->public_key_len,
                                          proof, proof_len,
                                          vault->receiver_context);
    }
    return false;
}
