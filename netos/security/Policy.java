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
 * 三档策略：open / readonly / sealed。
 *
 *   观测面   本机    open    读    写
 *   ---------------------------------------------------------
 *   open     放行   放行     放行  放行
 *   readonly 放行   放行     放行  需令牌
 *   sealed   放行   放行     需令牌 需令牌
 *
 * 为什么叫 readonly 而不是 guarded：
 *   这一档做的是「读公开、写验令牌」，不是「减弱版防护」。
 *   叫 guarded 会让人以为它比实际更严，反而容易把写接口当安全区。
 *   guarded 作为兼容别名保留（旧配置、服务器版），语义完全等同 readonly。
 *
 * 三条不可让步的规矩：
 *   1. 观测面白名单硬编码、逐条列举，绝不做前缀匹配。
 *      "/api/" 这种前缀会把将来的写接口一起放进去。
 *   2. 本机豁免只认 socket 地址；拿不到地址（"-"）不算本机。
 *      这里曾经写成「拿不到就当本机」，等于把 root 交出去。
 *   3. 令牌为空时不是「无密码」，是「一律不放行」。
 *      空串放行是最常见的一种后门，只是长得像配置失误。
 *
 * 明确不做的：令牌过期、撤销、并发会话。当前令牌是静态配置，
 * 轮换 = 重启。需要这些能力时应在传输层前加真正的会话层，而不是
 * 往这里塞字符串比较。
 */
public final class Policy implements Guard {

    public enum Mode { OPEN, READONLY, SEALED }

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

    /**
     * 解析配置。为兼容旧的 guarded，它映射到 READONLY；
     * 未知值一律落到 READONLY —— 打错一个字母不应该变成 open。
     */
    public static Mode parse(String raw) {
        if (raw == null || raw.isBlank()) {
            return Mode.READONLY;
        }
        return switch (raw.trim().toLowerCase()) {
            case "open" -> Mode.OPEN;
            case "sealed" -> Mode.SEALED;
            case "readonly", "guarded" -> Mode.READONLY;   // guarded = 已弃用别名
            default -> {
                Log.warn("security", "未知安全档位 %s，按 readonly 处理", raw);
                yield Mode.READONLY;
            }
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

        // 5. readonly 下的读放行
        if (mode == Mode.READONLY && read) {
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
        m.put("deprecatedAliases", java.util.List.of("guarded → readonly"));
        return m;
    }
}