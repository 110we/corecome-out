#ifndef COXVBN_KEY_VAULT_H
#define COXVBN_KEY_VAULT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#define COXVBN_KEY_MAX_PUBLIC_BYTES 128U
#define COXVBN_KEY_MAX_PROOF_BYTES 256U
#define COXVBN_RECEIVER_KEY_CAPACITY 32U

/* Existing verifier used by the isolated recovery authorization contract. */
typedef bool (*coxvbn_key_verify_fn)(const uint8_t *proof, size_t proof_len,
                                     void *context);

/* A platform crypto backend must verify proof-of-possession for public_key.
 * This callback is deliberately injected: the kernel does not claim to ship
 * a cryptographic primitive until an audited backend is integrated. */
typedef bool (*coxvbn_receiver_proof_fn)(
    const uint8_t *public_key, size_t public_key_len,
    const uint8_t *proof, size_t proof_len, void *context);

struct coxvbn_receiver_key_ref {
    uint64_t module_id;
    uint64_t receiver_id;
    uint64_t key_id;
    uint64_t key_version;
};

enum coxvbn_receiver_key_state {
    COXVBN_RECEIVER_KEY_UNUSED = 0,
    COXVBN_RECEIVER_KEY_ACTIVE = 1,
    COXVBN_RECEIVER_KEY_REVOKED = 2,
    /* Reserved while a verifier callback runs without the vault lock held. */
    COXVBN_RECEIVER_KEY_PENDING = 3,
    /* Old key is temporarily unavailable during an atomic rotation attempt. */
    COXVBN_RECEIVER_KEY_ROTATING = 4
};

struct coxvbn_receiver_key_record {
    struct coxvbn_receiver_key_ref ref;
    uint8_t public_key[COXVBN_KEY_MAX_PUBLIC_BYTES];
    size_t public_key_len;
    enum coxvbn_receiver_key_state state;
};

struct coxvbn_key_vault {
    coxvbn_key_verify_fn verify;
    void *context;
    bool initialized;
    coxvbn_receiver_proof_fn receiver_verify;
    void *receiver_context;
    uint64_t next_receiver_key_id;
    atomic_flag receiver_lock;
    struct coxvbn_receiver_key_record
        receiver_keys[COXVBN_RECEIVER_KEY_CAPACITY];
};

void coxvbn_key_vault_init(struct coxvbn_key_vault *vault,
                           coxvbn_key_verify_fn verify, void *context);
bool coxvbn_key_vault_verify(const struct coxvbn_key_vault *vault,
                             const uint8_t *proof, size_t proof_len);

void coxvbn_key_vault_set_receiver_verifier(
    struct coxvbn_key_vault *vault, coxvbn_receiver_proof_fn verify,
    void *context);

/* caller_module_id must be supplied by trusted kernel execution context,
 * never copied from an untrusted module request.
 *
 * Receiver verifier callbacks execute without the vault lock held. The
 * callback and its context must remain valid until any in-flight call ends. */

bool coxvbn_key_vault_enroll_receiver(
    struct coxvbn_key_vault *vault, uint64_t caller_module_id,
    uint64_t receiver_id, const uint8_t *public_key, size_t public_key_len,
    const uint8_t *proof, size_t proof_len,
    struct coxvbn_receiver_key_ref *out);

bool coxvbn_key_vault_rotate_receiver(
    struct coxvbn_key_vault *vault, uint64_t caller_module_id,
    const struct coxvbn_receiver_key_ref *old_ref,
    const uint8_t *new_public_key, size_t new_public_key_len,
    const uint8_t *proof, size_t proof_len,
    struct coxvbn_receiver_key_ref *out);

bool coxvbn_key_vault_revoke_receiver(
    struct coxvbn_key_vault *vault, uint64_t caller_module_id,
    const struct coxvbn_receiver_key_ref *ref);

/* Verification mutates lock state; the vault must therefore be non-const. */
bool coxvbn_key_vault_verify_receiver(
    struct coxvbn_key_vault *vault,
    const struct coxvbn_receiver_key_ref *ref,
    const uint8_t *proof, size_t proof_len);

#endif
