#include <assert.h>
#include <stdio.h>
#include "emergency_recovery.h"
#include "information_store.h"

static bool test_verify(const uint8_t *proof, size_t len, void *context)
{
    const uint8_t expected = *(const uint8_t *)context;
    return len == 1U && proof[0] == expected;
}

int main(void)
{
    struct coxvbn_emergency_recovery recovery;
    struct coxvbn_key_vault vault;
    struct coxvbn_information_store store;
    struct coxvbn_info_record record;
    const uint8_t expected = 0x5aU;
    const uint8_t good[] = {0x5aU};
    const uint8_t bad[] = {0x11U};

    coxvbn_recovery_init(&recovery);
    coxvbn_key_vault_init(&vault, test_verify, (void *)&expected);
    coxvbn_information_store_init(&store);

    assert(!coxvbn_recovery_port_enabled(&recovery));
    assert(!coxvbn_recovery_authorize(&recovery, &vault, good, sizeof(good),
                                     COXVBN_RECOVERY_CMD_REBOOT));
    coxvbn_recovery_enter_fault(&recovery);
    assert(coxvbn_recovery_port_enabled(&recovery));
    assert(!coxvbn_recovery_authorize(&recovery, &vault, bad, sizeof(bad),
                                     COXVBN_RECOVERY_CMD_REBOOT));
    assert(!coxvbn_recovery_authorize(&recovery, &vault, good, sizeof(good), 99U));
    assert(coxvbn_recovery_authorize(&recovery, &vault, good, sizeof(good),
                                    COXVBN_RECOVERY_CMD_DIAGNOSTICS));
    assert(coxvbn_information_store_append(&store, COXVBN_INFO_FAULT, 7U));
    assert(coxvbn_information_store_read(&store, 0U, &record));
    assert(record.kind == COXVBN_INFO_FAULT && record.code == 7U);
    coxvbn_recovery_leave_fault(&recovery);
    assert(!coxvbn_recovery_port_enabled(&recovery));
    puts("COXVBN recovery subsystem tests: PASS");
    return 0;
}
