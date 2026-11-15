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
    static const char *const expected[16] = {
        "CURRENT_SP0_SYNC", "CURRENT_SP0_IRQ",
        "CURRENT_SP0_FIQ", "CURRENT_SP0_SERROR",
        "LOWER_A64_SYNC", "LOWER_A64_IRQ",
        "LOWER_A64_FIQ", "LOWER_A64_SERROR",
        "LOWER_A32_SYNC", "LOWER_A32_IRQ",
        "LOWER_A32_FIQ", "LOWER_A32_SERROR",
        "CURRENT_SPX_SYNC", "CURRENT_SPX_IRQ",
        "CURRENT_SPX_FIQ", "CURRENT_SPX_SERROR"
    };
    unsigned int i;

    assert(strcmp(exception_class_name(esr_with_ec(0x15)),
                  "AArch64 SVC") == 0);
    assert(strcmp(exception_class_name(esr_with_ec(0x25)),
                  "Data abort at current EL") == 0);
    assert(strcmp(exception_class_name(esr_with_ec(0x2f)), "SError") == 0);
    assert(strcmp(exception_class_name(esr_with_ec(0x3c)),
                  "BRK instruction") == 0);
    assert(strcmp(exception_class_name(esr_with_ec(0x3f)),
                  "Unclassified exception") == 0);

    for (i = 0; i < 16U; ++i) {
        assert(strcmp(exception_vector_name(i), expected[i]) == 0);
    }
    assert(strcmp(exception_vector_name(16U), "INVALID_VECTOR") == 0);
    assert(strcmp(exception_vector_name(UINT64_MAX), "INVALID_VECTOR") == 0);
    return 0;
}
