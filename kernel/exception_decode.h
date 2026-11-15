#ifndef COXVBN_EXCEPTION_DECODE_H
#define COXVBN_EXCEPTION_DECODE_H

#include <stdint.h>

/* Return a stable diagnostic name for the ESR_EL1 exception class. */
const char *exception_class_name(uint64_t esr);

#endif
