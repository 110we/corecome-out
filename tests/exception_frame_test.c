#include "exception.h"

int main(void)
{
    struct exception_frame frame = {0};

    frame.x[30] = 0x30U;
    frame.elr = 0x1000U;
    frame.spsr = 0x3c5U;
    frame.esr = 0x96000005U;
    frame.far = 0x40000000U;

    if (frame.x[30] != 0x30U || frame.elr != 0x1000U ||
        frame.spsr != 0x3c5U || frame.esr != 0x96000005U ||
        frame.far != 0x40000000U) {
        return 1;
    }
    return 0;
}
