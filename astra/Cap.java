package astra;

import java.util.EnumSet;
import java.util.Set;

/**
 * 能力：不可伪造的授权凭证。
 *
 * NetOS 在 HTTP 层看"带没带令牌"，Astra 在总线上看"这条消息的权能覆不覆盖这个 topic"。
 * 三条约束：构造器私有（只能由 Astra 签发）、只能降权不能扩权、空能力不等于万能钥匙。
 */
public final class Cap {

    public enum Right { SEND, RECV, ADMIN }

    private final String id;
    private final String pattern;
    private final Set<Right> rights;
    private final long expiresAt;

    private Cap(String id, String pattern, Set<Right> rights, long expiresAt) {
        this.id = id;
        this.pattern = pattern == null ? "" : pattern;
        this.rights = rights.isEmpty() ? EnumSet.noneOf(Right.class) : EnumSet.copyOf(rights);
        this.expiresAt = expiresAt;
    }

    public static Cap issue(String id, String pattern, Set<Right> rights, long ttlMs) {
        long exp = ttlMs <= 0 ? 0 : System.currentTimeMillis() + ttlMs;
        return new Cap(id, pattern, rights, exp);
    }

    /** 空能力。pattern 为空串，covers 恒 false——不是"无限制"。 */
    public static Cap empty() {
        return new Cap("empty", "", EnumSet.noneOf(Right.class), 0);
    }

    public boolean allows(Right r) {
        return rights.contains(r);
    }

    public boolean expired() {
        return expiresAt != 0 && System.currentTimeMillis() > expiresAt;
    }

    /** 通配只有一种：结尾 * 表示前缀。不做正则。 */
    public boolean covers(String topic) {
        if (topic == null || pattern.isEmpty()) {
            return false;
        }
        if (pattern.endsWith("*")) {
            return topic.startsWith(pattern.substring(0, pattern.length() - 1));
        }
        return pattern.equals(topic);
    }

    /** 降权：只保留指定权限，且不延长有效期。 */
    public Cap downgrade(Right... keep) {
        EnumSet<Right> next = EnumSet.noneOf(Right.class);
        for (Right r : keep) {
            if (rights.contains(r)) {
                next.add(r);
            }
        }
        return new Cap(id + "·降权", pattern, next, expiresAt);
    }

    public String id() {
        return id;
    }

    public String pattern() {
        return pattern;
    }

    public Set<Right> rights() {
        return EnumSet.copyOf(rights);
    }

    public long expiresAt() {
        return expiresAt;
    }
}