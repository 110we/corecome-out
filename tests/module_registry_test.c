#include <assert.h>
#include <stdio.h>
#include <pthread.h>
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


#define REGISTRY_WORKERS 4U
#define REGISTRATIONS_PER_WORKER 7U

struct registry_worker {
    struct coxvbn_module_registry *registry;
    const void *authority;
    struct coxvbn_module_ref refs[REGISTRATIONS_PER_WORKER];
    bool ok;
};

static void *register_worker(void *argument)
{
    struct registry_worker *worker = argument;
    size_t i;
    worker->ok = true;
    for (i = 0U; i < REGISTRATIONS_PER_WORKER; ++i) {
        enum coxvbn_module_state state;
        if (!coxvbn_module_register(worker->registry, worker->authority,
                COXVBN_CAP_DEVICE_READ, COXVBN_CAP_DEVICE_READ,
                &worker->refs[i]) ||
            !coxvbn_module_resolve_identity(worker->registry,
                &worker->refs[i], NULL, NULL, &state) ||
            state != COXVBN_MODULE_PENDING) {
            worker->ok = false;
            break;
        }
    }
    return NULL;
}

static void test_concurrent_registry_registration(
    struct coxvbn_module_registry *registry, const void *authority)
{
    pthread_t threads[REGISTRY_WORKERS];
    struct registry_worker workers[REGISTRY_WORKERS];
    size_t i, j, a, b;

    for (i = 0U; i < REGISTRY_WORKERS; ++i) {
        workers[i].registry = registry;
        workers[i].authority = authority;
        workers[i].ok = false;
        assert(pthread_create(&threads[i], NULL, register_worker,
                              &workers[i]) == 0);
    }
    for (i = 0U; i < REGISTRY_WORKERS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        assert(workers[i].ok);
    }
    for (i = 0U; i < REGISTRY_WORKERS; ++i)
        for (a = 0U; a < REGISTRATIONS_PER_WORKER; ++a)
            for (j = i; j < REGISTRY_WORKERS; ++j)
                for (b = (j == i ? a + 1U : 0U);
                     b < REGISTRATIONS_PER_WORKER; ++b)
                    assert(workers[i].refs[a].module_id !=
                           workers[j].refs[b].module_id);
}

struct reentrant_verify_context {
    struct coxvbn_module_registry *registry;
    struct coxvbn_module_ref ref;
    bool called;
    bool observed_activating;
};
static bool reentrant_receiver_proof(
    const uint8_t *key, size_t key_len, const uint8_t *proof,
    size_t proof_len, void *opaque)
{
    struct reentrant_verify_context *ctx = opaque;
    enum coxvbn_module_state state = COXVBN_MODULE_UNUSED;
    ctx->called = true;
    ctx->observed_activating =
        coxvbn_module_resolve_identity(ctx->registry, &ctx->ref,
                                       NULL, NULL, &state) &&
        state == COXVBN_MODULE_ACTIVATING;
    return key != NULL && proof != NULL && key_len == 1U &&
           proof_len == 1U && key[0] == proof[0];
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
    struct reentrant_verify_context reentrant = {0};

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
    /* Legacy/system registration must not be a path around domain isolation. */
    assert(!coxvbn_module_register(
        &registry, &loader_authority,
        COXVBN_CAP_REDLAB_DISCOVERY, COXVBN_CAP_REDLAB_DISCOVERY, &rejected));
    assert(!coxvbn_module_register(
        &registry, &loader_authority,
        COXVBN_CAP_DAILY_PROFILE_DATA, COXVBN_CAP_DAILY_PROFILE_DATA, &rejected));
    assert(!coxvbn_module_register_domain(
        &registry, &loader_authority, COXVBN_DOMAIN_SYSTEM,
        COXVBN_CAP_REDLAB_ACTIVE_PROBE, COXVBN_CAP_REDLAB_ACTIVE_PROBE, &rejected));

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

    reentrant.registry = &registry;
    reentrant.ref = driver;
    coxvbn_key_vault_set_receiver_verifier(
        &vault, reentrant_receiver_proof, &reentrant);
    assert(coxvbn_module_activate(
        &registry, &loader_authority, &vault, &driver, 9U,
        key, sizeof(key), proof, sizeof(proof)));
    assert(reentrant.called && reentrant.observed_activating);
    coxvbn_key_vault_set_receiver_verifier(&vault, receiver_proof, NULL);
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

    assert(!coxvbn_module_register_domain(
        &registry, &loader_authority, COXVBN_DOMAIN_DAILY_PROFILE,
        COXVBN_CAP_REDLAB_ACTIVE_PROBE, COXVBN_CAP_REDLAB_ACTIVE_PROBE,
        &rejected));
    assert(!coxvbn_module_register_domain(
        &registry, &loader_authority, COXVBN_DOMAIN_REDLAB,
        COXVBN_CAP_DAILY_PROFILE_DATA, COXVBN_CAP_DAILY_PROFILE_DATA,
        &rejected));
    assert(coxvbn_module_register_domain(
        &registry, &loader_authority, COXVBN_DOMAIN_DAILY_PROFILE,
        COXVBN_CAP_DAILY_PROFILE_DATA | COXVBN_CAP_DAILY_PROFILE_SETTINGS,
        COXVBN_CAP_DAILY_PROFILE_DATA, &rejected));
    assert(!coxvbn_module_has_capability(
        &registry, &rejected, COXVBN_CAP_REDLAB_DISCOVERY));
    assert(coxvbn_module_register_domain(
        &registry, &loader_authority, COXVBN_DOMAIN_REDLAB,
        COXVBN_CAP_REDLAB_DISCOVERY | COXVBN_CAP_REDLAB_ACTIVE_PROBE,
        COXVBN_CAP_REDLAB_DISCOVERY, &rejected));
    assert(coxvbn_module_activate(
        &registry, &loader_authority, &vault, &rejected, 10U,
        key, sizeof(key), proof, sizeof(proof)));
    assert(coxvbn_module_authorize(
        &registry, &rejected, COXVBN_CAP_REDLAB_DISCOVERY));
    assert(!coxvbn_module_authorize(
        &registry, &rejected, COXVBN_CAP_REDLAB_ACTIVE_PROBE));
    assert(coxvbn_module_revoke(
        &registry, &loader_authority, &vault, &rejected));
    assert(!coxvbn_module_authorize(
        &registry, &rejected, COXVBN_CAP_REDLAB_DISCOVERY));

    test_concurrent_registry_registration(&registry, &loader_authority);

    puts("COXVBN module registry, concurrent registration, and Key Vault tests: PASS");
    return 0;
}
