#ifndef COXVBN_INFORMATION_STORE_H
#define COXVBN_INFORMATION_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define COXVBN_INFO_CAPACITY 32U

enum coxvbn_info_kind {
    COXVBN_INFO_BOOT = 1,
    COXVBN_INFO_FAULT = 2,
    COXVBN_INFO_RECOVERY = 3,
    COXVBN_INFO_SECURITY = 4
};

struct coxvbn_info_record {
    uint64_t sequence;
    uint32_t kind;
    uint32_t code;
};

struct coxvbn_information_store {
    struct coxvbn_info_record records[COXVBN_INFO_CAPACITY];
    uint64_t next_sequence;
    uint32_t count;
};

void coxvbn_information_store_init(struct coxvbn_information_store *store);
bool coxvbn_information_store_append(struct coxvbn_information_store *store,
                                     uint32_t kind, uint32_t code);
bool coxvbn_information_store_read(const struct coxvbn_information_store *store,
                                   uint32_t index,
                                   struct coxvbn_info_record *record);

#endif
