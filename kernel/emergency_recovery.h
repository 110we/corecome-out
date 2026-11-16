#ifndef COXVBN_EMERGENCY_RECOVERY_H
#define COXVBN_EMERGENCY_RECOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "key_vault.h"

#define COXVBN_RECOVERY_PORT 3000U

enum coxvbn_recovery_command {
    COXVBN_RECOVERY_CMD_REBOOT = 1,
    COXVBN_RECOVERY_CMD_DIAGNOSTICS = 2,
    COXVBN_RECOVERY_CMD_READ_LOGS = 3,
    COXVBN_RECOVERY_CMD_SYSTEM_RESET = 4
};

struct coxvbn_emergency_recovery {
    bool fault_mode;
    bool channel_enabled;
};

void coxvbn_recovery_init(struct coxvbn_emergency_recovery *recovery);
void coxvbn_recovery_enter_fault(struct coxvbn_emergency_recovery *recovery);
void coxvbn_recovery_leave_fault(struct coxvbn_emergency_recovery *recovery);
bool coxvbn_recovery_port_enabled(
    const struct coxvbn_emergency_recovery *recovery);
bool coxvbn_recovery_authorize(
    const struct coxvbn_emergency_recovery *recovery,
    const struct coxvbn_key_vault *vault,
    const uint8_t *proof, size_t proof_len, uint32_t command);

#endif
