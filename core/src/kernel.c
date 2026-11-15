#include "corecome/uart.h"
#include "corecome/security.h"
#include "corecome/security_plugin.h"
static void report_test(const char *name,bool passed){
 uart_puts(passed?"[PASS] ":"[FAIL] ");uart_puts(name);uart_puts("\n");
}
void kernel_main(void){
 struct security_plugin_request req={1,0xA001,0x1001,1000,
  SECURITY_OP_DISCOVER|SECURITY_OP_INSPECT,8};
 bool submitted,denied_pending,approved,allowed,wrong_tool,wrong_target,stopped,audit;
 uart_init();uart_puts("\nCORECOME standalone security kernel\n");
 uart_puts("arch: AArch64 | privilege: EL1 | platform: QEMU virt\n");
 security_init();security_plugin_init();
 denied_pending=!security_plugin_authorize(1,0xA001,0x1001,SECURITY_OP_DISCOVER,1);
 report_test("deny before plugin approval",denied_pending);
 submitted=security_plugin_submit(&req);
 report_test("central plugin accepts permission request",submitted);
 denied_pending=denied_pending&&!security_plugin_authorize(1,0xA001,0x1001,SECURITY_OP_DISCOVER,2);
 approved=security_plugin_user_decision(1,true,3);
 report_test("plugin issues grant after approval decision",approved);
 allowed=security_plugin_authorize(1,0xA001,0x1001,SECURITY_OP_DISCOVER,4);
 report_test("approved tool can use granted operation",allowed);
 wrong_tool=!security_plugin_authorize(1,0xB002,0x1001,SECURITY_OP_INSPECT,5);
 report_test("another tool cannot reuse grant",wrong_tool);
 wrong_target=!security_plugin_authorize(1,0xA001,0x2002,SECURITY_OP_DISCOVER,6);
 report_test("grant cannot be reused on another target",wrong_target);
 security_emergency_stop(7);
 stopped=security_is_stopped()&&!security_plugin_authorize(1,0xA001,0x1001,SECURITY_OP_INSPECT,8);
 report_test("emergency stop blocks privileged operations",stopped);
 audit=security_audit_count()>=6;
 report_test("kernel records grant allow denial and stop",audit);
 if(submitted&&denied_pending&&approved&&allowed&&wrong_tool&&wrong_target&&stopped&&audit)
  uart_puts("security plugin integration: PASS\n");
 else uart_puts("security plugin integration: FAIL\n");
 for(;;)__asm__ volatile("wfe");
}
