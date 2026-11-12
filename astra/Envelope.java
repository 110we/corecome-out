package astra;

import java.util.Map;

/**
 * 入队前的守门人：只查消息长得对不对，不查权限、不查路由。
 *
 * 定这些上限不为防攻击，只为让坏消息在入口就死掉——
 * 别让它在某个模块的深调用栈里炸成 NPE，再被记成"处理异常"。
 */
public final class Envelope {

    public static final int MAX_TOPIC = 128;
    public static final int MAX_COMMAND = 64;
    public static final int MAX_KEYS = 64;
    public static final int MAX_VALUE_CHARS = 8192;
    public static final int MAX_TOTAL_CHARS = 64 * 1024;

    private Envelope() {
    }

    public static SendResult check(Message m) {
        SendResult t = checkName(m.topic(), MAX_TOPIC, true);
        if (!t.ok()) {
            return t;
        }
        SendResult c = checkName(m.command(), MAX_COMMAND, false);
        if (!c.ok()) {
            return c;
        }
        return checkPayload(m.payload());
    }

    /** 发送目标是具体地址，不收通配符；订阅模式才允许 *。 */
    private static SendResult checkName(String s, int max, boolean topic) {
        SendResult bad = topic ? SendResult.BAD_TOPIC : SendResult.BAD_COMMAND;
        if (s == null || s.isEmpty() || s.length() > max) {
            return bad;
        }
        if (!isAlpha(s.charAt(0))) {
            return bad;
        }
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (!isAlpha(ch) && !Character.isDigit(ch) && ch != '.' && ch != '_' && ch != '-') {
                return bad;
            }
        }
        return SendResult.OK;
    }

    private static SendResult checkPayload(Map<String, Object> p) {
        if (p.size() > MAX_KEYS) {
            return SendResult.PAYLOAD_INVALID;
        }
        int total = 0;
        for (Map.Entry<String, Object> e : p.entrySet()) {
            String k = e.getKey();
            if (k == null || k.isEmpty() || k.length() > MAX_COMMAND) {
                return SendResult.PAYLOAD_INVALID;
            }
            Object v = e.getValue();
            if (v == null) {
                return SendResult.PAYLOAD_INVALID;
            }
            if (v instanceof String s) {
                total += s.length();
            } else if (v instanceof Number || v instanceof Boolean) {
                total += 8;
            } else if (v instanceof Map<?, ?> || v instanceof java.util.List<?>) {
                return SendResult.PAYLOAD_INVALID; // 嵌套一律不收：让契约保持在第一层
            } else {
                return SendResult.PAYLOAD_INVALID;
            }
            if (total > MAX_TOTAL_CHARS) {
                return SendResult.PAYLOAD_TOO_LARGE;
            }
        }
        return SendResult.OK;
    }

    private static boolean isAlpha(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }
}