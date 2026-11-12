package netos.security;

import netos.http.Request;
import netos.http.Response;
import netos.util.Log;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

/**
 * 三档策略：open / guarded / sealed。
 *
 *   观测面   本机    open    读   写
 *   ---------------------------------------------------------
 *   open     放行   放行    放行  放行
 *   guarded  放行   放行    放行  需令牌
 *   sealed   放行   放行    需令牌 需令牌
 *
 * 三条不可让步的规矩：
 *   1. 观测面白名单硬编码、逐条列举，绝不做前缀匹配。
 *      "/api/" 这种前缀会把将来的写接口一起放进去。
 *   2. 本机豁免只认 socket 地址；拿不到地址（"-"）不算本机。
 *      这里曾经写成「拿不到就当本机」，等于把 root 交出去。
 *   3. 令牌为空时不是「无密码」，是「一律不放行」。
 *      空串放行是最常见的一种后门，只是长得像配置失误。
 */
public final class Policy implements Guard {

    public enum Mode { OPEN, GUARDED, SEALED }

    private static final Set<String> OBSERVE = Set.of(
            "/", "/api/kernel", "/api/kernel/routes", "/api/kernel/config", "/api/security");

    private final Mode mode;
    private final String token;

    public Policy(Mode mode, String token) {
        this.mode = mode;
        this.token = token == null ? "" : token.trim();
        if (mode == Mode.SEALED && this.token.isEmpty()) {
            Log.warn("security", "sealed 模式未配置令牌：非本机的读写全部拒绝（不是放行）");
        }
    }

    public static Mode parse(String raw) {
        if (raw == null || raw.isBlank()) {
            return Mode.GUARDED;
        }
        return switch (raw.trim().toLowerCase()) {
            case "open" -> Mode.OPEN;
            case "sealed" -> Mode.SEALED;
            default -> Mode.GUARDED;
        };
    }

    @Override
    public Response check(Request req) {
        String path = req.path();
        boolean read = "GET".equalsIgnoreCase(req.method());

        // 1. 观测面：要么永不设闸，要么别声称自己在运维。
        if (read && OBSERVE.contains(path)) {
            return null;
        }

        // 2. 自指豁免。只认真实来源地址。
        if (isLocal(req.clientIp())) {
            return null;
        }

        // 3. 全开
        if (mode == Mode.OPEN) {
            return null;
        }

        // 4. 令牌（非空才算数）
        if (!token.isEmpty() && constantEquals(token, req.token())) {
            return null;
        }

        // 5. guarded 下的读放行
        if (mode == Mode.GUARDED && read) {
            return null;
        }

        Log.warn("security", "拒绝 %s %s ← %s（%s）", req.method(), path, req.clientIp(), mode);
        return Response.unauth("未授权：远端访问需携带 X-NetOS-Token 或 Authorization: Bearer");
    }

    /** 只认回环。拿不到地址给的是 "-"，不在此列。 */
    public static boolean isLocal(String ip) {
        if (ip == null) {
            return false;
        }
        return switch (ip) {
            case "127.0.0.1", "::1", "0:0:0:0:0:0:0:1", "localhost" -> true;
            default -> false;
        };
    }

    /** 定长比较，不提前返回：长度不同也走完整轮。 */
    private static boolean constantEquals(String expected, String actual) {
        return MessageDigest.isEqual(
                expected.getBytes(StandardCharsets.UTF_8),
                (actual == null ? "" : actual).getBytes(StandardCharsets.UTF_8));
    }

    public Mode mode() {
        return mode;
    }

    public Map<String, Object> describe() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("mode", mode.name().toLowerCase());
        m.put("tokenConfigured", !token.isEmpty());
        m.put("observeWhitelist", OBSERVE.stream().sorted().toList());
        m.put("localExempt", java.util.List.of("127.0.0.1", "::1"));
        return m;
    }
}