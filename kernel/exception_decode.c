#include "exception_decode.h"

const char *exception_class_name(uint64_t esr)
{
    switch ((unsigned int)((esr >> 26) & 0x3fU)) {
    case 0x00: return "Unknown";
    case 0x01: return "Trapped WFI/WFE";
    case 0x07: return "SVE access trap";
    case 0x0e: return "Illegal execution state";
    case 0x15: return "AArch64 SVC";
    case 0x18: return "Trapped MSR/MRS/system instruction";
    case 0x20: return "Instruction abort from lower EL";
    case 0x21: return "Instruction abort at current EL";
    case 0x22: return "PC alignment fault";
    case 0x24: return "Data abort from lower EL";
    case 0x25: return "Data abort at current EL";
    case 0x26: return "SP alignment fault";
    case 0x2f: return "SError";
    case 0x30: return "Breakpoint from lower EL";
    case 0x31: return "Breakpoint at current EL";
    case 0x32: return "Software step from lower EL";
    case 0x33: return "Software step at current EL";
    case 0x34: return "Watchpoint from lower EL";
    case 0x35: return "Watchpoint at current EL";
    case 0x3c: return "BRK instruction";
    default: return "Unclassified exception";
    }
}
