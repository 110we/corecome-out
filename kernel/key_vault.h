#ifndef COXVBN_KEY_VAULT_H
#define COXVBN_KEY_VAULT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A platform crypto/secure-storage backend must provide this verifier.
 * The vault never exposes stored key material to callers. */
typedef bool (*coxvbn_key_verify_fn)(const uint8_t *proof, size_t proof_len,
                                     void *context);

struct coxvbn_key_vault {
    coxvbn_key_verify_fn verify;
    void *context;
    bool initialized;
};

void coxvbn_key_vault_init(struct coxvbn_key_vault *vault,
                           coxvbn_key_verify_fn verify, void *context);
bool coxvbn_key_vault_verify(const struct coxvbn_key_vault *vault,
                             const uint8_t *proof, size_t proof_len);

#endif
