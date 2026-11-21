#ifndef CORECOME_EXCEPTION_H
#define CORECOME_EXCEPTION_H
#include <stdint.h>
void vectors_install(void);
void exception_handler(uint64_t kind, uint64_t esr, uint64_t far, uint64_t elr);
#endif