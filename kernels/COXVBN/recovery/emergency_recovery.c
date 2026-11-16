#include "emergency_recovery.h"

void coxvbn_recovery_init(struct coxvbn_emergency_recovery *recovery)
{
    if (recovery == 0) return;
    recovery->fault_mode = false;
    recovery->channel_enabled = false;
}

void coxvbn_recovery_enter_fault(struct coxvbn_emergency_recovery *recovery)
{
    if (recovery == 0) return;
    recovery->fault_mode = true;
    recovery->channel_enabled = true;
}

void coxvbn_recovery_leave_fault(struct coxvbn_emergency_recovery *recovery)
{
    if (recovery == 0) return;
    recovery->channel_enabled = false;
    recovery->fault_mode = false;
}

bool coxvbn_recovery_port_enabled(
    const struct coxvbn_emergency_recovery *recovery)
{
    return recovery != 0 && recovery->fault_mode && recovery->channel_enabled;
}

bool coxvbn_recovery_authorize(
    const struct coxvbn_emergency_recovery *recovery,
    const struct coxvbn_key_vault *vault,
    const uint8_t *proof, size_t proof_len, uint32_t command)
{
    if (!coxvbn_recovery_port_enabled(recovery)) return false;
    if (command < COXVBN_RECOVERY_CMD_REBOOT ||
        command > COXVBN_RECOVERY_CMD_SYSTEM_RESET)
        return false;
    return coxvbn_key_vault_verify(vault, proof, proof_len);
}
