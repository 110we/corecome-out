package astra.compat;

import astra.Astra;
import astra.Cap;
import astra.Message;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import netos.http.Request;
import netos.http.Response;
import netos.security.Policy;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executors;

/**
 * HTTP 面：与 netos.http.HttpService 同一套规矩（同一份 Policy / Request / Response）。
 *
 * 区别只有一处：NetOS 调 kernel.dispatch(req)，这里把请求翻成一条消息、等它回一条。
 * 传输层依然不认识安全，安全依然只吃 Request。
 */
public final class CompatEdge {

    /** 真实路由表。runtime 面的服务表就从这里刷——观测不到自己，就别谈观测。 */
    public static final List<String> ROUTES = List.of(
            "GET /", "GET /api/kernel", "GET /api/kernel/routes", "GET /api/kernel/config",
            "GET /api/kernel/sides", "GET /api/kernel/topics",
            "GET /api/security",
            "GET /api/life", "GET /api/life/host", "GET /api/life/heart", "POST /api/life/poke",
            "GET /api/runtime", "GET /api/runtime/services", "GET /api/runtime/jvm",
            "GET /api/runtime/history");

    private static final int MAX_BODY = 1024 * 1024;
    private static final long RPC_MS = 2000;

    private final Astra astra;
    private final Policy policy;
    private final int port;
    private final long tickMs;
    private final Cap face;
    private final RuntimeFace runtime = new RuntimeFace(ROUTES);
    private HttpServer server;

    public CompatEdge(Astra astra, Policy policy, int port, long tickMs) {
        this.astra = astra;
        this.policy = policy;
        this.port = port;
        this.tickMs = tickMs;
        this.face = astra.issue("life-face", "life.*", Cap.Right.SEND);
    }

    public void start() throws IOException {
        server = HttpServer.create(new InetSocketAddress(port), 128);
        server.createContext("/", this::handle);
        server.setExecutor(Executors.newFixedThreadPool(8, r -> {
            Thread t = new Thread(r, "astra-http");
            t.setDaemon(true);
            return t;
        }));
        server.start();

        Thread ticker = new Thread(() -> {
            while (true) {
                try {
                    Thread.sleep(tickMs);
                } catch (InterruptedException e) {
                    return;
                }
                astra.tick();
                runtime.beat();
                // 拍子也是消息：life 面的 beats 靠它长，不靠请求数充数。
                astra.bus().send(Message.of("kernel", "life.beat", "tick", Map.of(), face));
            }
        }, "astra-tick");
        ticker.setDaemon(true);
        ticker.start();
    }

    private void handle(HttpExchange ex) throws IOException {
        String method = ex.getRequestMethod().toUpperCase();
        String path = ex.getRequestURI().getPath();
        String query = ex.getRequestURI().getRawQuery();

        String ip = "-";
        if (ex.getRemoteAddress() != null && ex.getRemoteAddress().getAddress() != null) {
            ip = ex.getRemoteAddress().getAddress().getHostAddress();
        }

        String token = ex.getRequestHeaders().getFirst("X-NetOS-Token");
        String auth = ex.getRequestHeaders().getFirst("Authorization");
        if ((token == null || token.isBlank()) && auth != null && auth.startsWith("Bearer ")) {
            token = auth.substring(7).trim();
        }

        try {
            String body = readAll(ex.getRequestBody(), MAX_BODY);
            Request req = new Request(method, path, query, body, ip, token);
            Response denied = policy.check(req);
            if (denied != null) {
                write(ex, denied.httpStatus(), denied.json());
                return;
            }
            Response resp = dispatch(req);
            write(ex, resp.httpStatus(), resp.json());
        } catch (IllegalArgumentException tooLarge) {
            write(ex, 400, Response.bad(tooLarge.getMessage()).json());
        } catch (Throwable t) {
            write(ex, 500, Response.error("服务器内部错误").json());
        } finally {
            ex.close();
        }
    }

    private Response dispatch(Request req) {
        String key = req.method().toUpperCase() + " " + req.path();
        return switch (key) {
            case "GET /" -> Response.ok(root());
            case "GET /api/kernel" -> Response.ok(kernelView());
            case "GET /api/kernel/routes" -> Response.ok(routes());
            case "GET /api/kernel/config" -> Response.ok(config());
            case "GET /api/kernel/sides" -> Response.ok(Map.of(
                    "sides", List.of("life", "runtime"), "count", 2));
            case "GET /api/kernel/topics" -> Response.ok(Map.of(
                    "count", astra.bus().patterns().size(),
                    "topics", astra.bus().patterns().stream().sorted().toList()));
            case "GET /api/security" -> Response.ok(policy.describe());
            case "GET /api/life" -> rpc("life.info", "info", Map.of());
            case "GET /api/life/host" -> rpc("life.host", "host", Map.of());
            case "GET /api/life/heart" -> rpc("life.heart", "heart", Map.of());
            case "POST /api/life/poke" -> rpc("life.poke", "poke",
                    Map.of("from", req.clientIp(), "bodyLen", req.body().length()));
            case "GET /api/runtime" -> Response.ok(runtime.api());
            case "GET /api/runtime/services" -> Response.ok(runtime.services());
            case "GET /api/runtime/jvm" -> Response.ok(RuntimeFace.jvm());
            case "GET /api/runtime/history" -> Response.ok(runtime.history());
            default -> Response.notfound("无此路由: " + key);
        };
    }

    /** 一次请求 = 一条消息 + 一次答复。发不出去和没回音都要说清楚。 */
    private Response rpc(String topic, String command, Map<String, Object> payload) {
        Map<String, Object> data = astra.request("http", topic, command, payload, face, RPC_MS);
        if (data == null) {
            return Response.error("消息没有答复（超时或被内核拒收）");
        }
        return Response.ok(data);
    }

    private Map<String, Object> kernelView() {
        Map<String, Object> h = new LinkedHashMap<>(astra.health());
        h.put("alive", 2);
        h.put("routes", ROUTES.size());
        return h;
    }

    private Map<String, Object> root() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("name", "NetOS");
        m.put("version", "3.0.0-repo");
        m.put("port", port);
        m.put("tickMs", tickMs);
        m.put("strict", false);
        m.put("endpoints", ROUTES);
        return m;
    }

    private Map<String, Object> routes() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("count", ROUTES.size());
        m.put("routes", ROUTES);
        return m;
    }

    private Map<String, Object> config() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("kernel.port", port);
        m.put("kernel.tick.ms", tickMs);
        m.put("security.mode", policy.mode().name().toLowerCase());
        return m;
    }

    private static void write(HttpExchange ex, int status, String json) throws IOException {
        byte[] bytes = json.getBytes(StandardCharsets.UTF_8);
        ex.getResponseHeaders().set("Content-Type", "application/json; charset=utf-8");
        ex.getResponseHeaders().set("X-NetOS", "1");
        ex.sendResponseHeaders(status, bytes.length);
        try (OutputStream out = ex.getResponseBody()) {
            out.write(bytes);
        }
    }

    private static String readAll(InputStream in, int max) throws IOException {
        ByteArrayOutputStream buf = new ByteArrayOutputStream();
        byte[] chunk = new byte[8192];
        int total = 0;
        int n;
        while ((n = in.read(chunk)) > 0) {
            total += n;
            if (total > max) {
                throw new IllegalArgumentException("请求体超限，上限 " + (max / 1024) + " KB");
            }
            buf.write(chunk, 0, n);
        }
        return buf.toString(StandardCharsets.UTF_8);
    }

    public void stop() {
        if (server != null) {
            server.stop(1);
            server = null;
        }
    }
}