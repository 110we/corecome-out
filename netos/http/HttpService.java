package netos.http;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import netos.core.Kernel;
import netos.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.Executors;

/**
 * HTTP 传输层。
 *
 * 它不知道什么叫令牌、什么叫安全模式，只把请求交给 Kernel.dispatch。
 * 谁当闸门由 Main 决定 —— 传输层不认识安全，安全才可能被替换。
 *
 * 两条硬规矩：
 *   1. 请求体上限 1 MB，写死在常量里，不跟配置走。防弹衣不是开关。
 *   2. 来源 IP 只认 socket 的 remoteAddress。客户端塞的 X-Client-IP 一律不看。
 *      这条曾经是真实漏洞：拿不到 IP 就当本机，等于把 root 交出去。
 */
public final class HttpService {

    public static final int MAX_BODY_BYTES = 1024 * 1024;

    private final int port;
    private final Kernel kernel;
    private final netos.security.Guard guard;
    private HttpServer server;

    public HttpService(int port, Kernel kernel) {
        this(port, kernel, null);
    }

    /** guard 为 null 等于不设闸：只允许测试与本机绑定场景，生产必须传。 */
    public HttpService(int port, Kernel kernel, netos.security.Guard guard) {
        this.port = port;
        this.kernel = kernel;
        this.guard = guard;
    }

    public void start() throws IOException {
        if (port <= 0) {
            Log.warn("http", "端口未配置，跳过监听");
            return;
        }
        server = HttpServer.create(new InetSocketAddress(port), 128);
        server.createContext("/", this::handle);
        server.setExecutor(Executors.newFixedThreadPool(8, r -> {
            Thread t = new Thread(r, "netos-http");
            t.setDaemon(true);
            return t;
        }));
        server.start();
        Log.info("http", "已监听 0.0.0.0:%d，%d 条路由", port, kernel.routeCount());
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
            String body = readAll(ex.getRequestBody(), MAX_BODY_BYTES);
            Request req = new Request(method, path, query, body, ip, token);
            if (guard != null) {
                Response denied = guard.check(req);
                if (denied != null) {
                    write(ex, denied.httpStatus(), denied.json());
                    return;
                }
            }
            Response resp = kernel.dispatch(req);
            write(ex, resp.httpStatus(), resp.json());
        } catch (IllegalArgumentException tooLarge) {
            write(ex, 400, Response.bad(tooLarge.getMessage()).json());
        } catch (Throwable t) {
            Log.errorStack("http", method + " " + path + " 处理异常", t);
            write(ex, 500, Response.error("服务器内部错误").json());
        } finally {
            ex.close();
        }
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

    /** 读到上限为止；超了直接拒，不截断 —— 截断会让调用方以为成功。 */
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
            Log.info("http", "已停止监听 %d", port);
        }
    }
}