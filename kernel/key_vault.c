#include "key_vault.h"

void coxvbn_key_vault_init(struct coxvbn_key_vault *vault,
                           coxvbn_key_verify_fn verify, void *context)
{
    if (vault == NULL) return;
    vault->verify = verify;
    vault->context = context;
    vault->initialized = (verify != NULL);
}

bool coxvbn_key_vault_verify(const struct coxvbn_key_vault *vault,
                             const uint8_t *proof, size_t proof_len)
{
    if (vault == NULL || !vault->initialized || vault->verify == NULL ||
        proof == NULL || proof_len == 0U)
        return false;
    return vault->verify(proof, proof_len, vault->context);
}
