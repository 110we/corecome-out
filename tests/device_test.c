#include "device.h"

#include <assert.h>
#include <string.h>

struct mock_device {
    int probes, removes, suspends, resumes, fail_probe, lookup_during_probe;
    struct coxvbn_device_registry *registry;
    const char *probe_name;
};
static int probe(void *p)
{
    struct mock_device *m = p;
    struct coxvbn_device_ref ref;
    ++m->probes;
    if (m->registry != 0 && m->probe_name != 0)
        m->lookup_during_probe =
            coxvbn_device_lookup(m->registry, m->probe_name, &ref);
    return m->fail_probe;
}
static int remove_device(void *p) { ++((struct mock_device *)p)->removes; return 0; }
static int suspend_device(void *p) { ++((struct mock_device *)p)->suspends; return 0; }
static int resume_device(void *p) { ++((struct mock_device *)p)->resumes; return 0; }

int main(void)
{
    static const struct coxvbn_device_ops ops = {
        probe, remove_device, suspend_device, resume_device
    };
    struct coxvbn_device_registry registry;
    struct coxvbn_device_ref ref, found, stale;
    enum coxvbn_device_state state;
    struct mock_device mock = {0};
    uint32_t i;
    char name[COXVBN_DEVICE_NAME_MAX + 1U];

    coxvbn_device_registry_init(&registry);
    mock.registry = &registry;
    mock.probe_name = "virtio-net0";
    assert(coxvbn_device_register(&registry, "virtio-net0",
        COXVBN_DEVICE_CLASS_NETWORK, &ops, &mock, &ref) == 0);
    assert(mock.probes == 1 && registry.count == 1U);
    assert(mock.lookup_during_probe == -2);
    assert(coxvbn_device_lookup(&registry, "virtio-net0", &found) == 0);
    assert(found.id == ref.id && found.generation == ref.generation);
    assert(coxvbn_device_register(&registry, "virtio-net0",
        COXVBN_DEVICE_CLASS_NETWORK, &ops, &mock, &found) == -3);
    assert(coxvbn_device_suspend(&registry, &ref) == 0);
    assert(coxvbn_device_get_state(&registry, &ref, &state, 0) == 0);
    assert(state == COXVBN_DEVICE_SUSPENDED);
    assert(coxvbn_device_suspend(&registry, &ref) == -1);
    assert(coxvbn_device_resume(&registry, &ref) == 0);
    assert(coxvbn_device_get_state(&registry, &ref, &state, 0) == 0);
    assert(state == COXVBN_DEVICE_ACTIVE);
    assert(coxvbn_device_unregister(&registry, &ref) == 0);
    assert(mock.removes == 1 && registry.count == 0U);
    stale = ref;
    assert(coxvbn_device_get_state(&registry, &stale, &state, 0) == -1);

    mock.fail_probe = -9;
    assert(coxvbn_device_register(&registry, "bad-device",
        COXVBN_DEVICE_CLASS_PLATFORM, &ops, &mock, &found) == -9);
    assert(registry.count == 0U);
    mock.fail_probe = 0;
    for (i = 0U; i < COXVBN_DEVICE_CAPACITY; ++i) {
        name[0] = 'd';
        name[1] = (char)('0' + (i / 10U));
        name[2] = (char)('0' + (i % 10U));
        name[3] = '\0';
        assert(coxvbn_device_register(&registry, name,
            COXVBN_DEVICE_CLASS_PLATFORM, &ops, &mock, &found) == 0);
    }
    assert(registry.count == COXVBN_DEVICE_CAPACITY);
    assert(coxvbn_device_register(&registry, "overflow",
        COXVBN_DEVICE_CLASS_PLATFORM, &ops, &mock, &found) == -4);
    assert(coxvbn_device_register(&registry, "", COXVBN_DEVICE_CLASS_PLATFORM,
        &ops, &mock, &found) == -1);
    return 0;
}
