package netos.http;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * 统一响应：{code, message, data, ts}。
 *
 * code 是业务码（0 成功），HTTP 状态码独立 —— 两者不能混用。
 * 混用之后，"查询为空的成功"和"权限不足的失败"在日志里会长得一样。
 */
public final class Response {

    public final int code;
    public final String message;
    public final Object data;
    public final long ts = System.currentTimeMillis();

    private Response(int code, String message, Object data) {
        this.code = code;
        this.message = message;
        this.data = data;
    }

    public static Response of(int code, String message, Object data) {
        return new Response(code, message, data);
    }

    public static Response ok(Object data) {
        return new Response(0, "ok", data);
    }

    public static Response bad(String message) {
        return of(400, message, null);
    }

    public static Response unauth(String message) {
        return of(401, message, null);
    }

    public static Response notfound(String message) {
        return of(404, message, null);
    }

    public static Response error(String message) {
        return of(500, message, null);
    }

    /** HTTP 状态码，由业务码映射。观测类失败仍给 200 以外的真实码。 */
    public int httpStatus() {
        if (code == 0) {
            return 200;
        }
        return switch (code) {
            case 400 -> 400;
            case 401, 403 -> 401;
            case 404 -> 404;
            default -> 500;
        };
    }

    public Map<String, Object> asMap() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("code", code);
        m.put("message", message);
        m.put("data", data);
        m.put("ts", ts);
        return m;
    }

    public String json() {
        return stringify(asMap());
    }

    @SuppressWarnings("unchecked")
    private static String stringify(Object v) {
        if (v == null) {
            return "null";
        }
        if (v instanceof String s) {
            return quote(s);
        }
        if (v instanceof Number || v instanceof Boolean) {
            return v.toString();
        }
        if (v instanceof Map<?, ?> map) {
            StringBuilder sb = new StringBuilder("{");
            boolean first = true;
            for (Map.Entry<?, ?> e : ((Map<Object, Object>) map).entrySet()) {
                if (!first) {
                    sb.append(',');
                }
                first = false;
                sb.append(quote(String.valueOf(e.getKey()))).append(':').append(stringify(e.getValue()));
            }
            return sb.append('}').toString();
        }
        if (v instanceof List<?> list) {
            StringBuilder sb = new StringBuilder("[");
            for (int i = 0; i < list.size(); i++) {
                if (i > 0) {
                    sb.append(',');
                }
                sb.append(stringify(list.get(i)));
            }
            return sb.append(']').toString();
        }
        return quote(String.valueOf(v));
    }

    private static String quote(String s) {
        StringBuilder sb = new StringBuilder("\"");
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"' -> sb.append("\\\"");
                case '\\' -> sb.append("\\\\");
                case '\n' -> sb.append("\\n");
                case '\r' -> sb.append("\\r");
                case '\t' -> sb.append("\\t");
                default -> {
                    if (c < 0x20) {
                        sb.append(String.format("\\u%04x", (int) c));
                    } else {
                        sb.append(c);
                    }
                }
            }
        }
        return sb.append('"').toString();
    }
}