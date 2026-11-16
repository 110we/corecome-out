#include <assert.h>
#include <stdio.h>
#include "key_vault.h"

static bool test_recovery_proof(const uint8_t *proof, size_t len, void *ctx)
{
    return len == 1U && proof[0] == *(const uint8_t *)ctx;
}

static bool test_receiver_proof(const uint8_t *key, size_t key_len,
                                const uint8_t *proof, size_t proof_len,
                                void *ctx)
{
    (void)ctx;
    return key != NULL && key_len == 1U && proof_len == 1U &&
           proof[0] == key[0];
}

int main(void)
{
    struct coxvbn_key_vault vault;
    struct coxvbn_receiver_key_ref first, second, third;
    const uint8_t recovery_secret = 0x5aU;
    const uint8_t recovery_proof[] = {0x5aU};
    const uint8_t key_one[] = {0x11U};
    const uint8_t proof_one[] = {0x11U};
    const uint8_t key_two[] = {0x22U};
    const uint8_t proof_two[] = {0x22U};
    const uint8_t key_three[] = {0x33U};
    const uint8_t proof_three[] = {0x33U};
    const uint8_t bad_proof[] = {0xffU};

    coxvbn_key_vault_init(&vault, test_recovery_proof,
                          (void *)&recovery_secret);
    assert(coxvbn_key_vault_verify(&vault, recovery_proof,
                                   sizeof(recovery_proof)));
    coxvbn_key_vault_set_receiver_verifier(&vault, test_receiver_proof, NULL);

    assert(!coxvbn_key_vault_enroll_receiver(
        &vault, 7U, 9U, key_one, sizeof(key_one), bad_proof,
        sizeof(bad_proof), &first));
    assert(coxvbn_key_vault_enroll_receiver(
        &vault, 7U, 9U, key_one, sizeof(key_one), proof_one,
        sizeof(proof_one), &first));
    assert(first.module_id == 7U && first.receiver_id == 9U);
    assert(first.key_version == 1U);
    assert(coxvbn_key_vault_verify_receiver(&vault, &first, proof_one,
                                            sizeof(proof_one)));
    assert(!coxvbn_key_vault_revoke_receiver(&vault, 8U, &first));

    assert(coxvbn_key_vault_rotate_receiver(
        &vault, 7U, &first, key_two, sizeof(key_two), proof_two,
        sizeof(proof_two), &second));
    assert(second.key_version == 2U);
    assert(!coxvbn_key_vault_verify_receiver(&vault, &first, proof_one,
                                             sizeof(proof_one)));
    assert(!coxvbn_key_vault_rotate_receiver(
        &vault, 7U, &first, key_three, sizeof(key_three), proof_three,
        sizeof(proof_three), &third));
    assert(coxvbn_key_vault_verify_receiver(&vault, &second, proof_two,
                                            sizeof(proof_two)));

    assert(coxvbn_key_vault_revoke_receiver(&vault, 7U, &second));
    assert(!coxvbn_key_vault_verify_receiver(&vault, &second, proof_two,
                                             sizeof(proof_two)));
    assert(!coxvbn_key_vault_revoke_receiver(&vault, 7U, &second));

    puts("COXVBN receiver key lifecycle tests: PASS");
    return 0;
}
