#include "corecome/uart.h"
#include "corecome/security.h"

static void report_test(const char *name, bool passed)
{
    uart_puts(passed ? "[PASS] " : "[FAIL] ");
    uart_puts(name);
    uart_puts("\n");
}

void kernel_main(void)
{
    bool denied_by_default;
    bool grant_ok;
    bool allowed_in_scope;
    bool denied_out_of_scope;
    bool stopped_denied;
    bool audit_present;

    uart_init();
    uart_puts("\nCORECOME standalone security kernel\n");
    uart_puts("arch: AArch64 | privilege: EL1 | platform: QEMU virt\n");

    security_init();

    denied_by_default =
        !security_authorize(1, 0x1001, SECURITY_OP_DISCOVER, 1);
    report_test("deny without authorization scope", denied_by_default);

    grant_ok = security_scope_grant(
        1, 0x1001,
        SECURITY_OP_DISCOVER | SECURITY_OP_INSPECT,
        1000, 8, 2);
    report_test("grant bounded assessment scope", grant_ok);

    allowed_in_scope =
        security_authorize(1, 0x1001, SECURITY_OP_DISCOVER, 3);
    report_test("allow permitted operation on scoped target",
                allowed_in_scope);

    denied_out_of_scope =
        !security_authorize(1, 0x2002, SECURITY_OP_DISCOVER, 4);
    report_test("deny operation outside target scope",
                denied_out_of_scope);

    security_emergency_stop(5);
    stopped_denied =
        security_is_stopped() &&
        !security_authorize(1, 0x1001, SECURITY_OP_INSPECT, 6);
    report_test("emergency stop revokes further operations",
                stopped_denied);

    audit_present = security_audit_count() >= 6;
    report_test("record grants, allows, denials and stop in audit ring",
                audit_present);

    if (denied_by_default && grant_ok && allowed_in_scope &&
        denied_out_of_scope && stopped_denied && audit_present) {
        uart_puts("security core: PASS\n");
    } else {
        uart_puts("security core: FAIL\n");
    }

    for (;;) {
        __asm__ volatile("wfe");
    }
}
