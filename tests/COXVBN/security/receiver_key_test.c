#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include "key_vault.h"

struct reentrant_context {
    struct coxvbn_key_vault *vault;
    struct coxvbn_receiver_key_ref revoke_ref;
    bool revoke_once;
};

static bool test_recovery_proof(const uint8_t *proof, size_t len, void *ctx)
{
    return len == 1U && proof[0] == *(const uint8_t *)ctx;
}

static bool test_receiver_proof(const uint8_t *key, size_t key_len,
                                const uint8_t *proof, size_t proof_len,
                                void *ctx)
{
    struct reentrant_context *reentrant = ctx;
    if (reentrant != NULL && reentrant->revoke_once) {
        reentrant->revoke_once = false;
        /* Re-entry would deadlock if the verifier ran under receiver_lock. */
        assert(coxvbn_key_vault_revoke_receiver(
            reentrant->vault, reentrant->revoke_ref.module_id,
            &reentrant->revoke_ref));
    }
    return key != NULL && key_len == 1U && proof_len == 1U &&
           proof[0] == key[0];
}

#define ENROLLS_PER_THREAD 20U
struct enroll_worker {
    struct coxvbn_key_vault *vault;
    uint64_t module_id;
    unsigned int successes;
    struct coxvbn_receiver_key_ref refs[ENROLLS_PER_THREAD];
};

static void *enroll_worker_main(void *opaque)
{
    struct enroll_worker *worker = opaque;
    unsigned int i;
    for (i = 0U; i < ENROLLS_PER_THREAD; ++i) {
        uint8_t key = (uint8_t)(worker->module_id * 31U + i + 1U);
        if (coxvbn_key_vault_enroll_receiver(
                worker->vault, worker->module_id, (uint64_t)i + 1U,
                &key, 1U, &key, 1U, &worker->refs[i]))
            ++worker->successes;
    }
    return NULL;
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
    struct reentrant_context reentrant;
    pthread_t threads[2];
    struct enroll_worker workers[2];
    unsigned int total_successes;

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

    /* Callback re-entry must not hold the vault lock or deadlock. */
    reentrant.vault = &vault;
    reentrant.revoke_ref = first;
    reentrant.revoke_once = true;
    coxvbn_key_vault_set_receiver_verifier(
        &vault, test_receiver_proof, &reentrant);
    assert(coxvbn_key_vault_enroll_receiver(
        &vault, 7U, 10U, key_two, sizeof(key_two), proof_two,
        sizeof(proof_two), &second));
    assert(!coxvbn_key_vault_verify_receiver(&vault, &first, proof_one,
                                             sizeof(proof_one)));

    coxvbn_key_vault_set_receiver_verifier(&vault, test_receiver_proof, NULL);
    /* Failed proof must roll ROTATING back to ACTIVE and preserve old key. */
    assert(!coxvbn_key_vault_rotate_receiver(
        &vault, 7U, &second, key_three, sizeof(key_three), bad_proof,
        sizeof(bad_proof), &third));
    assert(coxvbn_key_vault_verify_receiver(&vault, &second, proof_two,
                                            sizeof(proof_two)));
    /* A subsequent valid rotation must still succeed. */
    assert(coxvbn_key_vault_rotate_receiver(
        &vault, 7U, &second, key_three, sizeof(key_three), proof_three,
        sizeof(proof_three), &third));
    assert(third.key_version == 2U);
    assert(!coxvbn_key_vault_verify_receiver(&vault, &second, proof_two,
                                             sizeof(proof_two)));
    assert(coxvbn_key_vault_verify_receiver(&vault, &third, proof_three,
                                            sizeof(proof_three)));
    assert(coxvbn_key_vault_revoke_receiver(&vault, 7U, &third));
    assert(!coxvbn_key_vault_verify_receiver(&vault, &third, proof_three,
                                             sizeof(proof_three)));
    assert(!coxvbn_key_vault_revoke_receiver(&vault, 7U, &third));

    /* Two concurrent writers must never allocate the same slot or key id. */
    coxvbn_key_vault_init(&vault, test_recovery_proof,
                          (void *)&recovery_secret);
    coxvbn_key_vault_set_receiver_verifier(&vault, test_receiver_proof, NULL);
    workers[0].vault = &vault;
    workers[0].module_id = 21U;
    workers[0].successes = 0U;
    workers[1].vault = &vault;
    workers[1].module_id = 22U;
    workers[1].successes = 0U;
    assert(pthread_create(&threads[0], NULL, enroll_worker_main,
                          &workers[0]) == 0);
    assert(pthread_create(&threads[1], NULL, enroll_worker_main,
                          &workers[1]) == 0);
    assert(pthread_join(threads[0], NULL) == 0);
    assert(pthread_join(threads[1], NULL) == 0);
    total_successes = workers[0].successes + workers[1].successes;
    assert(total_successes == COXVBN_RECEIVER_KEY_CAPACITY);

    puts("COXVBN receiver key lifecycle/concurrency tests: PASS");
    return 0;
}
