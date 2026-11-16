#include <assert.h>
#include <stdio.h>
#include "module_registry.h"

static bool receiver_proof(const uint8_t *key, size_t key_len,
                           const uint8_t *proof, size_t proof_len,
                           void *context)
{
    (void)context;
    return key != NULL && key_len == 1U && proof != NULL &&
           proof_len == 1U && key[0] == proof[0];
}

static bool recovery_proof(const uint8_t *proof, size_t len, void *context)
{
    (void)context;
    return proof != NULL && len == 1U && proof[0] == 0x5aU;
}

int main(void)
{
    struct coxvbn_module_registry registry;
    struct coxvbn_key_vault vault;
    struct coxvbn_module_ref driver, ipc, rejected;
    static const int loader_authority = 42;
    static const int forged_authority = 42;
    const uint8_t key[] = {0x31U};
    const uint8_t proof[] = {0x31U};
    const uint8_t bad_proof[] = {0xffU};

    coxvbn_key_vault_init(&vault, recovery_proof, NULL);
    coxvbn_key_vault_set_receiver_verifier(&vault, receiver_proof, NULL);
    coxvbn_module_registry_init(&registry, &loader_authority);

    assert(!coxvbn_module_register(
        &registry, &forged_authority,
        COXVBN_CAP_DEVICE_READ, COXVBN_CAP_DEVICE_READ, &rejected));
    assert(!coxvbn_module_register(
        &registry, &loader_authority,
        COXVBN_CAP_DEVICE_READ, COXVBN_CAP_DEVICE_WRITE, &rejected));
    assert(!coxvbn_module_register(
        &registry, &loader_authority,
        UINT64_C(1) << 40, COXVBN_CAP_NONE, &rejected));

    assert(coxvbn_module_register(
        &registry, &loader_authority,
        COXVBN_CAP_DEVICE_READ | COXVBN_CAP_DEVICE_WRITE,
        COXVBN_CAP_DEVICE_READ, &driver));
    assert(driver.module_id == 1U);
    assert(!coxvbn_module_is_active(&registry, &driver));
    assert(!coxvbn_module_authorize(
        &registry, &driver, COXVBN_CAP_DEVICE_READ));
    assert(!coxvbn_module_activate(
        &registry, &forged_authority, &vault, &driver, 9U,
        key, sizeof(key), proof, sizeof(proof)));
    assert(!coxvbn_module_activate(
        &registry, &loader_authority, &vault, &driver, 9U,
        key, sizeof(key), bad_proof, sizeof(bad_proof)));
    assert(!coxvbn_module_is_active(&registry, &driver));
    assert(!coxvbn_module_authorize(
        &registry, &driver, COXVBN_CAP_DEVICE_READ));

    assert(coxvbn_module_activate(
        &registry, &loader_authority, &vault, &driver, 9U,
        key, sizeof(key), proof, sizeof(proof)));
    assert(coxvbn_module_is_active(&registry, &driver));
    assert(coxvbn_module_authorize(
        &registry, &driver, COXVBN_CAP_DEVICE_READ));
    assert(!coxvbn_module_authorize(
        &registry, &driver, COXVBN_CAP_DEVICE_WRITE));
    assert(!coxvbn_module_activate(
        &registry, &loader_authority, &vault, &driver, 9U,
        key, sizeof(key), proof, sizeof(proof)));

    assert(coxvbn_module_register(
        &registry, &loader_authority,
        COXVBN_CAP_IPC_SEND | COXVBN_CAP_IPC_RECEIVE,
        COXVBN_CAP_IPC_SEND | COXVBN_CAP_IPC_RECEIVE, &ipc));
    assert(ipc.module_id == 2U);
    assert(!coxvbn_module_authorize(
        &registry, &ipc, COXVBN_CAP_IPC_SEND));
    assert(coxvbn_module_revoke(
        &registry, &loader_authority, NULL, &ipc));
    assert(!coxvbn_module_is_active(&registry, &ipc));
    assert(!coxvbn_module_revoke(
        &registry, &loader_authority, NULL, &ipc));

    assert(!coxvbn_module_revoke(
        &registry, &forged_authority, &vault, &driver));
    assert(coxvbn_module_revoke(
        &registry, &loader_authority, &vault, &driver));
    assert(!coxvbn_module_is_active(&registry, &driver));
    assert(!coxvbn_module_authorize(
        &registry, &driver, COXVBN_CAP_DEVICE_READ));
    assert(!coxvbn_key_vault_verify_receiver(
        &vault, &registry.modules[0].receiver_key, proof, sizeof(proof)));
    assert(!coxvbn_module_revoke(
        &registry, &loader_authority, &vault, &driver));

    puts("COXVBN module registry and Key Vault activation tests: PASS");
    return 0;
}
