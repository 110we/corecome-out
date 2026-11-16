#ifndef COXVBN_MODULE_REGISTRY_H
#define COXVBN_MODULE_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COXVBN_MODULE_CAPACITY 32U

typedef uint64_t coxvbn_capability_t;

#define COXVBN_CAP_NONE          UINT64_C(0)
#define COXVBN_CAP_DEVICE_READ   (UINT64_C(1) << 0)
#define COXVBN_CAP_DEVICE_WRITE  (UINT64_C(1) << 1)
#define COXVBN_CAP_MEMORY_MAP    (UINT64_C(1) << 2)
#define COXVBN_CAP_IPC_SEND      (UINT64_C(1) << 3)
#define COXVBN_CAP_IPC_RECEIVE   (UINT64_C(1) << 4)
#define COXVBN_CAP_KEY_ENROLL    (UINT64_C(1) << 5)
#define COXVBN_CAP_KEY_ROTATE    (UINT64_C(1) << 6)
#define COXVBN_CAP_RECOVERY      (UINT64_C(1) << 7)
#define COXVBN_CAP_ALL           ((UINT64_C(1) << 8) - UINT64_C(1))

enum coxvbn_module_state {
    COXVBN_MODULE_UNUSED = 0,
    COXVBN_MODULE_ACTIVE = 1,
    COXVBN_MODULE_REVOKED = 2
};

struct coxvbn_module_ref {
    uint64_t module_id;
    uint64_t generation;
};

struct coxvbn_module_record {
    struct coxvbn_module_ref ref;
    coxvbn_capability_t requested;
    coxvbn_capability_t granted;
    enum coxvbn_module_state state;
};

struct coxvbn_module_registry {
    /* Identity of a kernel-owned authority object; never expose it to modules. */
    const void *loader_authority;
    uint64_t next_module_id;
    struct coxvbn_module_record modules[COXVBN_MODULE_CAPACITY];
};

void coxvbn_module_registry_init(struct coxvbn_module_registry *registry,
                                 const void *loader_authority);

/* Kernel loader only. granted must be a subset of requested and known caps. */
bool coxvbn_module_register(
    struct coxvbn_module_registry *registry,
    const void *loader_authority,
    coxvbn_capability_t requested,
    coxvbn_capability_t granted,
    struct coxvbn_module_ref *out);

bool coxvbn_module_is_active(
    const struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref);

bool coxvbn_module_has_capability(
    const struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref,
    coxvbn_capability_t required);

bool coxvbn_module_authorize(
    const struct coxvbn_module_registry *registry,
    const struct coxvbn_module_ref *ref,
    coxvbn_capability_t required);

bool coxvbn_module_revoke(
    struct coxvbn_module_registry *registry,
    const void *loader_authority,
    const struct coxvbn_module_ref *ref);

#endif
