package netos.http;

/**
 * 请求。
 *
 * 只有四样东西：方法、路径、查询串、正文。
 * 刻意不提供「读某个请求头就顺手信了它」的入口 ——
 * 来源 IP 由 HttpService 在建请求时用 remoteAddress 覆盖，
 * 客户端自己塞的头一律不算数。
 */
public final class Request {

    private final String method;
    private final String path;
    private final String query;
    private final String body;
    private final String clientIp;
    private final String token;

    public Request(String method, String path, String query, String body,
                   String clientIp, String token) {
        this.method = method;
        this.path = path;
        this.query = query == null ? "" : query;
        this.body = body == null ? "" : body;
        this.clientIp = clientIp == null || clientIp.isBlank() ? "-" : clientIp;
        this.token = token == null ? "" : token;
    }

    public String method() {
        return method;
    }

    public String path() {
        return path;
    }

    public String query() {
        return query;
    }

    public String body() {
        return body;
    }

    /** 真实来源 IP，- 表示拿不到。判本机时必须把 - 当作「不是本机」。 */
    public String clientIp() {
        return clientIp;
    }

    public String token() {
        return token;
    }

    public String param(String key) {
        if (query.isEmpty()) {
            return "";
        }
        for (String pair : query.split("&")) {
            int eq = pair.indexOf('=');
            String k = eq < 0 ? pair : pair.substring(0, eq);
            if (k.equals(key)) {
                return eq < 0 ? "" : pair.substring(eq + 1);
            }
        }
        return "";
    }
}