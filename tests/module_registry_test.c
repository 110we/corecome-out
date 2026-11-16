#include <assert.h>
#include <stdio.h>
#include "module_registry.h"

int main(void)
{
    struct coxvbn_module_registry registry;
    struct coxvbn_module_ref driver, ipc, rejected;
    static const int loader_authority = 42;
    static const int forged_authority = 42;

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
    assert(coxvbn_module_is_active(&registry, &driver));
    assert(coxvbn_module_authorize(
        &registry, &driver, COXVBN_CAP_DEVICE_READ));
    assert(!coxvbn_module_authorize(
        &registry, &driver, COXVBN_CAP_DEVICE_WRITE));
    assert(!coxvbn_module_authorize(
        &registry, &driver,
        COXVBN_CAP_DEVICE_READ | COXVBN_CAP_DEVICE_WRITE));
    assert(!coxvbn_module_authorize(
        &registry, &driver, COXVBN_CAP_NONE));

    assert(coxvbn_module_register(
        &registry, &loader_authority,
        COXVBN_CAP_IPC_SEND | COXVBN_CAP_IPC_RECEIVE,
        COXVBN_CAP_IPC_SEND | COXVBN_CAP_IPC_RECEIVE, &ipc));
    assert(ipc.module_id == 2U);
    assert(coxvbn_module_authorize(
        &registry, &ipc, COXVBN_CAP_IPC_SEND | COXVBN_CAP_IPC_RECEIVE));

    assert(!coxvbn_module_revoke(
        &registry, &forged_authority, &driver));
    assert(coxvbn_module_revoke(&registry, &loader_authority, &driver));
    assert(!coxvbn_module_is_active(&registry, &driver));
    assert(!coxvbn_module_authorize(
        &registry, &driver, COXVBN_CAP_DEVICE_READ));
    assert(!coxvbn_module_revoke(&registry, &loader_authority, &driver));

    puts("COXVBN module registry and capability tests: PASS");
    return 0;
}
