#ifndef COXVBN_DEVICE_H
#define COXVBN_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#define COXVBN_DEVICE_CAPACITY 32U
#define COXVBN_DEVICE_NAME_MAX 31U

enum coxvbn_device_class {
    COXVBN_DEVICE_CLASS_PLATFORM = 0,
    COXVBN_DEVICE_CLASS_NETWORK,
    COXVBN_DEVICE_CLASS_STORAGE,
    COXVBN_DEVICE_CLASS_INPUT,
    COXVBN_DEVICE_CLASS_CONSOLE
};

enum coxvbn_device_state {
    COXVBN_DEVICE_UNUSED = 0,
    COXVBN_DEVICE_ACTIVE,
    COXVBN_DEVICE_SUSPENDED
};

struct coxvbn_device_ref {
    uint32_t id;
    uint32_t generation;
};

struct coxvbn_device_ops {
    int (*probe)(void *context);
    int (*remove)(void *context);
    int (*suspend)(void *context);
    int (*resume)(void *context);
};

struct coxvbn_device_record {
    char name[COXVBN_DEVICE_NAME_MAX + 1U];
    struct coxvbn_device_ref ref;
    const struct coxvbn_device_ops *ops;
    void *context;
    enum coxvbn_device_class device_class;
    enum coxvbn_device_state state;
    int last_error;
};

struct coxvbn_device_registry {
    uint32_t next_id;
    uint32_t generation;
    uint32_t count;
    struct coxvbn_device_record devices[COXVBN_DEVICE_CAPACITY];
};

/* Registry operations are serialized by the caller; IRQ/SMP safety is not
 * provided until the kernel synchronization layer is integrated. */
void coxvbn_device_registry_init(struct coxvbn_device_registry *registry);
int coxvbn_device_register(struct coxvbn_device_registry *registry,
                           const char *name,
                           enum coxvbn_device_class device_class,
                           const struct coxvbn_device_ops *ops,
                           void *context,
                           struct coxvbn_device_ref *ref_out);
int coxvbn_device_unregister(struct coxvbn_device_registry *registry,
                             const struct coxvbn_device_ref *ref);
int coxvbn_device_suspend(struct coxvbn_device_registry *registry,
                          const struct coxvbn_device_ref *ref);
int coxvbn_device_resume(struct coxvbn_device_registry *registry,
                         const struct coxvbn_device_ref *ref);
int coxvbn_device_lookup(const struct coxvbn_device_registry *registry,
                         const char *name,
                         struct coxvbn_device_ref *ref_out);
int coxvbn_device_get_state(const struct coxvbn_device_registry *registry,
                            const struct coxvbn_device_ref *ref,
                            enum coxvbn_device_state *state_out,
                            int *last_error_out);

#endif
