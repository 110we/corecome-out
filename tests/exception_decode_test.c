#include "exception_decode.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint64_t esr_with_ec(unsigned int ec)
{
    return (uint64_t)ec << 26;
}

int main(void)
{
    assert(strcmp(exception_class_name(esr_with_ec(0x15)),
                  "AArch64 SVC") == 0);
    assert(strcmp(exception_class_name(esr_with_ec(0x25)),
                  "Data abort at current EL") == 0);
    assert(strcmp(exception_class_name(esr_with_ec(0x2f)), "SError") == 0);
    assert(strcmp(exception_class_name(esr_with_ec(0x3c)),
                  "BRK instruction") == 0);
    assert(strcmp(exception_class_name(esr_with_ec(0x3f)),
                  "Unclassified exception") == 0);
    return 0;
}
