#ifndef CORECOME_SECURITY_INTERNAL_H
#define CORECOME_SECURITY_INTERNAL_H
#include <stdbool.h>
#include <stdint.h>
/* Private kernel/plugin boundary. Not part of the public tool API. */
bool security_monitor_install_grant(uint64_t grant_id,uint64_t requester_id,
 uint64_t target_id,uint32_t operations,uint64_t expires_at,
 uint32_t action_limit,uint64_t now);
#endif
