#ifndef COXVBN_EXCEPTION_DECODE_H
#define COXVBN_EXCEPTION_DECODE_H

#include <stdint.h>

/* Stable diagnostic names for architectural ESR and VBAR_EL1 fields. */
const char *exception_class_name(uint64_t esr);
const char *exception_vector_name(uint64_t vector);

#endif
