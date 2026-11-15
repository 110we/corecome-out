#include "corecome/uart.h"
#include "corecome/security.h"
#include "corecome/security_plugin.h"

static void report_test(const char *name, bool passed)
{
    uart_puts(passed ? "[PASS] " : "[FAIL] ");
    uart_puts(name);
    uart_puts("\n");
}

void kernel_main(void)
{
    struct security_plugin_request req = {
        1, 0xA001, 0x1001, 1000,
        SECURITY_OP_DISCOVER | SECURITY_OP_INSPECT, 8
    };
    struct security_plugin_request denied_req = {
        2, 0xA001, 0x1001, 1000, SECURITY_OP_CAPTURE, 2
    };
    bool submitted, denied_pending, approved, allowed;
    bool wrong_tool, wrong_target, revoked, denied_by_user;
    bool stopped, audit_present;

    uart_init();
    uart_puts("\nCORECOME standalone security kernel\n");
    uart_puts("arch: AArch64 | privilege: EL1 | platform: QEMU virt\n");

    security_init();
    security_plugin_init();

    denied_pending = !security_plugin_authorize(
        1, 0xA001, 0x1001, SECURITY_OP_DISCOVER, 1);
    report_test("deny before plugin approval", denied_pending);

    submitted = security_plugin_submit(&req);
    report_test("central plugin accepts permission request", submitted);

    denied_pending = denied_pending && !security_plugin_authorize(
        1, 0xA001, 0x1001, SECURITY_OP_DISCOVER, 2);
    report_test("pending request cannot execute privileged operation",
                denied_pending);

    approved = security_plugin_user_decision(1, true, 3);
    report_test("plugin issues grant after approval", approved);

    allowed = security_plugin_authorize(
        1, 0xA001, 0x1001, SECURITY_OP_DISCOVER, 4);
    report_test("approved tool can use granted operation", allowed);

    wrong_tool = !security_plugin_authorize(
        1, 0xB002, 0x1001, SECURITY_OP_INSPECT, 5);
    report_test("another tool cannot reuse grant", wrong_tool);

    wrong_target = !security_plugin_authorize(
        1, 0xA001, 0x2002, SECURITY_OP_DISCOVER, 6);
    report_test("grant cannot be reused on another target", wrong_target);

    security_plugin_revoke(7);
    revoked = !security_plugin_authorize(
        1, 0xA001, 0x1001, SECURITY_OP_INSPECT, 8);
    report_test("plugin revocation blocks further operations", revoked);

    denied_by_user = security_plugin_submit(&denied_req) &&
        security_plugin_user_decision(2, false, 9) &&
        !security_plugin_authorize(
            2, 0xA001, 0x1001, SECURITY_OP_CAPTURE, 10);
    report_test("user denial never creates a usable grant", denied_by_user);

    security_emergency_stop(11);
    stopped = security_is_stopped() && !security_plugin_authorize(
        2, 0xA001, 0x1001, SECURITY_OP_INSPECT, 12);
    report_test("emergency stop blocks privileged operations", stopped);

    audit_present = security_audit_count() >= 8;
    report_test("kernel records grants, allows, denials and revocation",
                audit_present);

    if (submitted && denied_pending && approved && allowed && wrong_tool &&
        wrong_target && revoked && denied_by_user && stopped && audit_present) {
        uart_puts("security plugin integration: PASS\n");
        uart_puts("boot: PASS\n");
    } else {
        uart_puts("security plugin integration: FAIL\n");
        uart_puts("boot: FAIL\n");
    }

    for (;;) {
        __asm__ volatile("wfe");
    }
}
