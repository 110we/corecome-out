#include "information_store.h"

void coxvbn_information_store_init(struct coxvbn_information_store *store)
{
    uint32_t i;
    if (store == 0) return;
    store->next_sequence = 1U;
    store->count = 0U;
    for (i = 0; i < COXVBN_INFO_CAPACITY; ++i) {
        store->records[i].sequence = 0U;
        store->records[i].kind = 0U;
        store->records[i].code = 0U;
    }
}

bool coxvbn_information_store_append(struct coxvbn_information_store *store,
                                     uint32_t kind, uint32_t code)
{
    uint32_t slot;
    if (store == 0 || kind < COXVBN_INFO_BOOT || kind > COXVBN_INFO_SECURITY)
        return false;
    slot = (uint32_t)((store->next_sequence - 1U) % COXVBN_INFO_CAPACITY);
    store->records[slot].sequence = store->next_sequence++;
    store->records[slot].kind = kind;
    store->records[slot].code = code;
    if (store->count < COXVBN_INFO_CAPACITY) ++store->count;
    return true;
}

bool coxvbn_information_store_read(const struct coxvbn_information_store *store,
                                   uint32_t index,
                                   struct coxvbn_info_record *record)
{
    uint32_t oldest;
    uint32_t slot;
    if (store == 0 || record == 0 || index >= store->count) return false;
    oldest = (uint32_t)((store->next_sequence - 1U - store->count) %
                        COXVBN_INFO_CAPACITY);
    slot = (oldest + index) % COXVBN_INFO_CAPACITY;
    *record = store->records[slot];
    return true;
}
