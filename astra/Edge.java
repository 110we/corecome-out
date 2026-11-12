package astra;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;

import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * HTTP 边界。唯一允许把外部请求翻译成消息的地方——内核不认识 HTTP。
 *
 * 门外只有一件事要判断：这个请求自称持有什么。剩下的判定全在总线里。
 */
public final class Edge {

    private static final String CAP_HEADER = "X-Astra-Cap";

    private final Astra astra;
    private final int port;
    private final ExecutorService pool = Executors.newFixedThreadPool(4);
    private HttpServer server;

    public Edge(Astra astra, int port) {
        this.astra = astra;
        this.port = port;
    }

    public void start() throws IOException {
        server = HttpServer.create(new InetSocketAddress(port), 0);
        server.createContext("/", this::route);
        server.setExecutor(pool);
        server.start();
    }

    public void stop() {
        if (server != null) {
            server.stop(0);
        }
        pool.shutdownNow();
    }

    private void route(HttpExchange ex) throws IOException {
        String path = ex.getRequestURI().getPath();
        String method = ex.getRequestMethod();
        String[] p = path.split("/");
        try {
            if ("/health".equals(path) && "GET".equals(method)) {
                reply(ex, 200, astra.health());
                return;
            }
            if ("/dead".equals(path) && "GET".equals(method)) {
                reply(ex, 200, Map.of("dead", astra.bus().deadLetters()));
                return;
            }
            if (p.length >= 4 && "cmd".equals(p[1]) && "POST".equals(method)) {
                Cap cap = astra.cap(ex.getRequestHeaders().getFirst(CAP_HEADER));
                if (cap == null) {
                    reply(ex, 403, Map.of("error", "cap 不存在或已撤销"));
                    return;
                }
                String body = new String(ex.getRequestBody().readAllBytes(), StandardCharsets.UTF_8);
                int prio = priority(ex);
                SendResult r = astra.bus().send(
                        Message.of("edge", p[2], p[3], Map.of("raw", body), cap, prio));
                if (!r.ok()) {
                    reply(ex, r.http(), Map.of("error", r.reason(), "topic", p[2]));
                    return;
                }
                reply(ex, 202, Map.of("accepted", true, "topic", p[2],
                        "command", p[3], "priority", prio));
                return;
            }
            if (p.length >= 3 && "revoke".equals(p[1]) && "POST".equals(method)) {
                Cap cap = astra.cap(ex.getRequestHeaders().getFirst(CAP_HEADER));
                if (cap == null || !cap.allows(Cap.Right.ADMIN)) {
                    reply(ex, 403, Map.of("error", "需要 ADMIN 能力"));
                    return;
                }
                boolean gone = astra.revoke(p[2]);
                reply(ex, gone ? 200 : 404, Map.of("revoked", gone, "id", p[2]));
                return;
            }
            if (p.length >= 3 && "replay".equals(p[1]) && "POST".equals(method)) {
                Cap cap = astra.cap(ex.getRequestHeaders().getFirst(CAP_HEADER));
                if (cap == null || !cap.allows(Cap.Right.ADMIN)) {
                    reply(ex, 403, Map.of("error", "需要 ADMIN 能力"));
                    return;
                }
                long deadSeq;
                try {
                    deadSeq = Long.parseLong(p[2]);
                } catch (NumberFormatException bad) {
                    reply(ex, 400, Map.of("error", "死信编号不是数字"));
                    return;
                }
                java.util.Optional<SendResult> r = astra.bus().replay(deadSeq);
                if (r.isEmpty()) {
                    reply(ex, 404, Map.of("error", "死信里没有这条"));
                    return;
                }
                reply(ex, r.get().http(), Map.of("replayed", r.get().ok(),
                        "result", r.get().reason()));
                return;
            }
            if (p.length >= 4 && "breaker".equals(p[1]) && "reset".equals(p[3])
                    && "POST".equals(method)) {
                Cap cap = astra.cap(ex.getRequestHeaders().getFirst(CAP_HEADER));
                if (cap == null || !cap.allows(Cap.Right.ADMIN)) {
                    reply(ex, 403, Map.of("error", "需要 ADMIN 能力"));
                    return;
                }
                astra.bus().resetBreaker(p[2]);
                reply(ex, 200, Map.of("reset", p[2]));
                return;
            }
            if ("/audit".equals(path) && "GET".equals(method)) {
                reply(ex, 200, Map.of("audit", astra.bus().auditTail(50)));
                return;
            }
            reply(ex, 404, Map.of("error", "no such route"));
        } catch (RuntimeException e) {
            reply(ex, 500, Map.of("error", e.getClass().getSimpleName()));
        }
    }

    /** 缺省 USER；非法值也按 USER——不因为一个错头把消息抬成系统级。 */
    private static int priority(HttpExchange ex) {
        String raw = ex.getRequestHeaders().getFirst("X-Astra-Priority");
        if (raw == null) {
            return Message.PRIORITY_USER;
        }
        try {
            return Math.max(0, Math.min(9, Integer.parseInt(raw.trim())));
        } catch (NumberFormatException e) {
            return Message.PRIORITY_USER;
        }
    }

    private void reply(HttpExchange ex, int status, Object body) throws IOException {
        byte[] bytes = toJson(body).getBytes(StandardCharsets.UTF_8);
        ex.getResponseHeaders().set("Content-Type", "application/json; charset=utf-8");
        ex.sendResponseHeaders(status, bytes.length);
        try (OutputStream os = ex.getResponseBody()) {
            os.write(bytes);
        }
    }

    private static String toJson(Object o) {
        if (o == null) {
            return "null";
        }
        if (o instanceof String s) {
            return '"' + s.replace("\\", "\\\\").replace("\"", "\\\"")
                    .replace("\n", "\\n") + '"';
        }
        if (o instanceof Number || o instanceof Boolean) {
            return o.toString();
        }
        if (o instanceof Map<?, ?> m) {
            StringBuilder sb = new StringBuilder("{");
            m.forEach((k, v) -> {
                if (sb.length() > 1) {
                    sb.append(',');
                }
                sb.append(toJson(String.valueOf(k))).append(':').append(toJson(v));
            });
            return sb.append('}').toString();
        }
        if (o instanceof List<?> list) {
            StringBuilder sb = new StringBuilder("[");
            for (Object v : list) {
                if (sb.length() > 1) {
                    sb.append(',');
                }
                sb.append(toJson(v));
            }
            return sb.append(']').toString();
        }
        return toJson(String.valueOf(o));
    }
}