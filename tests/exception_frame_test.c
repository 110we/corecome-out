#include "exception.h"

int main(void)
{
    struct exception_frame frame = {0};
    unsigned int vector;

    frame.x[30] = 0x30U;
    frame.elr = 0x1000U;
    frame.spsr = 0x3c5U;
    frame.esr = 0x96000005U;
    frame.far = 0x40000000U;

    for (vector = 0; vector < 16U; ++vector) {
        frame.vector = vector;
        if (frame.vector != vector) return 1;
    }

    if (frame.x[30] != 0x30U || frame.elr != 0x1000U ||
        frame.spsr != 0x3c5U || frame.esr != 0x96000005U ||
        frame.far != 0x40000000U || frame.vector != 15U) {
        return 1;
    }
    return 0;
}
