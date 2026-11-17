#include <assert.h>
#include <stdio.h>
#include "kern.h"

int fdt_test_compat_has(const u8 *p, u32 len, const char *want);

int main(void)
{
    static const u8 valid[] = {
        'v','e','n','d','o','r',0,
        'a','r','m',',','p','l','0','1','1',0
    };
    static const u8 prefix_only[] = {
        'a','r','m',',','p','l','0','1','1','-','extra',0
    };
    static const u8 unterminated[] = {
        'a','r','m',',','p','l','0','1','1'
    };
    static const u8 valid_then_bad_tail[] = {
        'a','r','m',',','p','l','0','1','1',0,'x','y'
    };

    assert(fdt_test_compat_has(valid, sizeof(valid), "arm,pl011"));
    assert(!fdt_test_compat_has(valid, sizeof(valid), "arm,gic-400"));
    assert(!fdt_test_compat_has(prefix_only, sizeof(prefix_only), "arm,pl011"));
    assert(!fdt_test_compat_has(unterminated, sizeof(unterminated), "arm,pl011"));
    /* A valid prefix must not hide a malformed tail in the string list. */
    assert(!fdt_test_compat_has(valid_then_bad_tail,
                                sizeof(valid_then_bad_tail), "arm,pl011"));

    puts("FDT compatible-string bounds: PASS");
    return 0;
}
