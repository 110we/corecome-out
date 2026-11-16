#include "key_vault.h"

static void vault_lock(struct coxvbn_key_vault *vault)
{
    while (atomic_flag_test_and_set_explicit(&vault->receiver_lock,
                                              memory_order_acquire)) { }
}

static void vault_unlock(struct coxvbn_key_vault *vault)
{
    atomic_flag_clear_explicit(&vault->receiver_lock, memory_order_release);
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, size_t len)
{
    size_t i;
    for (i = 0U; i < len; ++i) dst[i] = src[i];
}

static void clear_record(struct coxvbn_receiver_key_record *record)
{
    size_t i;
    record->ref.module_id = 0U;
    record->ref.receiver_id = 0U;
    record->ref.key_id = 0U;
    record->ref.key_version = 0U;
    for (i = 0U; i < COXVBN_KEY_MAX_PUBLIC_BYTES; ++i)
        record->public_key[i] = 0U;
    record->public_key_len = 0U;
    record->state = COXVBN_RECEIVER_KEY_UNUSED;
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

/* All record lookup and allocation helpers below require receiver_lock. */
static struct coxvbn_receiver_key_record *find_record_locked(
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

static struct coxvbn_receiver_key_record *free_record_locked(
    struct coxvbn_key_vault *vault)
{
    size_t i;
    for (i = 0U; i < COXVBN_RECEIVER_KEY_CAPACITY; ++i) {
        if (vault->receiver_keys[i].state == COXVBN_RECEIVER_KEY_UNUSED)
            return &vault->receiver_keys[i];
    }
    return NULL;
}

static bool reserve_record_locked(
    struct coxvbn_key_vault *vault, uint64_t module_id,
    uint64_t receiver_id, uint64_t key_version,
    struct coxvbn_receiver_key_record **record_out)
{
    struct coxvbn_receiver_key_record *record = free_record_locked(vault);
    if (record == NULL || vault->next_receiver_key_id == 0U)
        return false;
    clear_record(record);
    record->ref.module_id = module_id;
    record->ref.receiver_id = receiver_id;
    record->ref.key_id = vault->next_receiver_key_id++;
    record->ref.key_version = key_version;
    record->state = COXVBN_RECEIVER_KEY_PENDING;
    *record_out = record;
    return true;
}

void coxvbn_key_vault_init(struct coxvbn_key_vault *vault,
                           coxvbn_key_verify_fn verify, void *context)
{
    size_t i;
    if (vault == NULL) return;
    atomic_flag_clear_explicit(&vault->receiver_lock, memory_order_relaxed);
    vault->verify = verify;
    vault->context = context;
    vault->initialized = (verify != NULL);
    vault->receiver_verify = NULL;
    vault->receiver_context = NULL;
    vault->next_receiver_key_id = 1U;
    for (i = 0U; i < COXVBN_RECEIVER_KEY_CAPACITY; ++i)
        clear_record(&vault->receiver_keys[i]);
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
    vault_lock(vault);
    vault->receiver_verify = verify;
    vault->receiver_context = context;
    vault_unlock(vault);
}

bool coxvbn_key_vault_enroll_receiver(
    struct coxvbn_key_vault *vault, uint64_t caller_module_id,
    uint64_t receiver_id, const uint8_t *public_key, size_t public_key_len,
    const uint8_t *proof, size_t proof_len,
    struct coxvbn_receiver_key_ref *out)
{
    uint8_t key_copy[COXVBN_KEY_MAX_PUBLIC_BYTES];
    uint8_t proof_copy[COXVBN_KEY_MAX_PROOF_BYTES];
    coxvbn_receiver_proof_fn verifier;
    void *verifier_context;
    struct coxvbn_receiver_key_record *reserved;
    struct coxvbn_receiver_key_ref reserved_ref;
    bool verified, committed = false;

    if (vault == NULL || !vault->initialized || caller_module_id == 0U ||
        receiver_id == 0U || out == NULL ||
        !valid_key_input(public_key, public_key_len, proof, proof_len))
        return false;
    copy_bytes(key_copy, public_key, public_key_len);
    copy_bytes(proof_copy, proof, proof_len);

    vault_lock(vault);
    verifier = vault->receiver_verify;
    verifier_context = vault->receiver_context;
    if (verifier == NULL ||
        !reserve_record_locked(vault, caller_module_id, receiver_id, 1U,
                               &reserved)) {
        vault_unlock(vault);
        return false;
    }
    reserved_ref = reserved->ref;
    vault_unlock(vault);

    /* Never invoke platform crypto while holding the vault spinlock. */
    verified = verifier(key_copy, public_key_len, proof_copy, proof_len,
                        verifier_context);

    vault_lock(vault);
    reserved = find_record_locked(vault, &reserved_ref);
    if (reserved != NULL && reserved->state == COXVBN_RECEIVER_KEY_PENDING) {
        if (verified) {
            copy_bytes(reserved->public_key, key_copy, public_key_len);
            reserved->public_key_len = public_key_len;
            reserved->state = COXVBN_RECEIVER_KEY_ACTIVE;
            *out = reserved->ref;
            committed = true;
        } else {
            clear_record(reserved);
        }
    }
    vault_unlock(vault);
    return committed;
}

bool coxvbn_key_vault_rotate_receiver(
    struct coxvbn_key_vault *vault, uint64_t caller_module_id,
    const struct coxvbn_receiver_key_ref *old_ref,
    const uint8_t *new_public_key, size_t new_public_key_len,
    const uint8_t *proof, size_t proof_len,
    struct coxvbn_receiver_key_ref *out)
{
    uint8_t key_copy[COXVBN_KEY_MAX_PUBLIC_BYTES];
    uint8_t proof_copy[COXVBN_KEY_MAX_PROOF_BYTES];
    coxvbn_receiver_proof_fn verifier;
    void *verifier_context;
    struct coxvbn_receiver_key_record *old_record, *new_record;
    struct coxvbn_receiver_key_ref new_ref;
    bool verified, committed = false;

    if (vault == NULL || !vault->initialized || old_ref == NULL ||
        out == NULL || caller_module_id == 0U ||
        caller_module_id != old_ref->module_id ||
        old_ref->key_version == UINT64_MAX ||
        !valid_key_input(new_public_key, new_public_key_len, proof, proof_len))
        return false;
    copy_bytes(key_copy, new_public_key, new_public_key_len);
    copy_bytes(proof_copy, proof, proof_len);

    vault_lock(vault);
    verifier = vault->receiver_verify;
    verifier_context = vault->receiver_context;
    old_record = find_record_locked(vault, old_ref);
    if (verifier == NULL || old_record == NULL ||
        old_record->state != COXVBN_RECEIVER_KEY_ACTIVE ||
        !reserve_record_locked(vault, old_ref->module_id,
                               old_ref->receiver_id,
                               old_ref->key_version + 1U, &new_record)) {
        vault_unlock(vault);
        return false;
    }
    new_ref = new_record->ref;
    old_record->state = COXVBN_RECEIVER_KEY_ROTATING;
    vault_unlock(vault);

    verified = verifier(key_copy, new_public_key_len, proof_copy, proof_len,
                        verifier_context);

    vault_lock(vault);
    old_record = find_record_locked(vault, old_ref);
    new_record = find_record_locked(vault, &new_ref);
    if (old_record != NULL && new_record != NULL &&
        old_record->state == COXVBN_RECEIVER_KEY_ROTATING &&
        new_record->state == COXVBN_RECEIVER_KEY_PENDING) {
        if (verified) {
            copy_bytes(new_record->public_key, key_copy, new_public_key_len);
            new_record->public_key_len = new_public_key_len;
            new_record->state = COXVBN_RECEIVER_KEY_ACTIVE;
            old_record->state = COXVBN_RECEIVER_KEY_REVOKED;
            *out = new_record->ref;
            committed = true;
        } else {
            clear_record(new_record);
            old_record->state = COXVBN_RECEIVER_KEY_ACTIVE;
        }
    }
    vault_unlock(vault);
    return committed;
}

bool coxvbn_key_vault_revoke_receiver(
    struct coxvbn_key_vault *vault, uint64_t caller_module_id,
    const struct coxvbn_receiver_key_ref *ref)
{
    struct coxvbn_receiver_key_record *record;
    if (vault == NULL || !vault->initialized || ref == NULL ||
        caller_module_id == 0U || caller_module_id != ref->module_id)
        return false;
    vault_lock(vault);
    record = find_record_locked(vault, ref);
    if (record == NULL || record->state != COXVBN_RECEIVER_KEY_ACTIVE) {
        vault_unlock(vault);
        return false;
    }
    record->state = COXVBN_RECEIVER_KEY_REVOKED;
    vault_unlock(vault);
    return true;
}

bool coxvbn_key_vault_verify_receiver(
    struct coxvbn_key_vault *vault,
    const struct coxvbn_receiver_key_ref *ref,
    const uint8_t *proof, size_t proof_len)
{
    uint8_t key_copy[COXVBN_KEY_MAX_PUBLIC_BYTES];
    uint8_t proof_copy[COXVBN_KEY_MAX_PROOF_BYTES];
    size_t key_len;
    coxvbn_receiver_proof_fn verifier;
    void *verifier_context;
    bool verified, still_active = false;

    if (vault == NULL || !vault->initialized || ref == NULL ||
        proof == NULL || proof_len == 0U ||
        proof_len > COXVBN_KEY_MAX_PROOF_BYTES)
        return false;
    copy_bytes(proof_copy, proof, proof_len);

    vault_lock(vault);
    verifier = vault->receiver_verify;
    verifier_context = vault->receiver_context;
    {
        struct coxvbn_receiver_key_record *record =
            find_record_locked(vault, ref);
        if (verifier == NULL || record == NULL ||
            record->state != COXVBN_RECEIVER_KEY_ACTIVE) {
            vault_unlock(vault);
            return false;
        }
        key_len = record->public_key_len;
        copy_bytes(key_copy, record->public_key, key_len);
    }
    vault_unlock(vault);

    verified = verifier(key_copy, key_len, proof_copy, proof_len,
                        verifier_context);

    /* A concurrent revoke/rotation wins over an in-flight verification. */
    vault_lock(vault);
    {
        struct coxvbn_receiver_key_record *record =
            find_record_locked(vault, ref);
        still_active = record != NULL &&
                       record->state == COXVBN_RECEIVER_KEY_ACTIVE;
    }
    vault_unlock(vault);
    return verified && still_active;
}
