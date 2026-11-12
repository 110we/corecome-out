package netos;

import netos.core.Config;
import netos.core.Kernel;
import netos.core.SideState;
import netos.http.HttpService;
import netos.http.Response;
import netos.security.Policy;
import netos.sides.LifeSide;
import netos.sides.RuntimeSide;
import netos.util.Log;

import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.CountDownLatch;

/**
 * 启动入口。
 *
 * 只做四件事：读参数、装配置、装面、起监听。
 * 装配顺序与失败策略全部交给 Kernel —— 入口越薄，越不容易变成第二个内核。
 */
public final class Main {

    public static void main(String[] args) throws Exception {
        Map<String, String> argv = parse(args);
        String conf = argv.getOrDefault("conf", "conf/kernel.conf");

        Config config = new Config(Path.of(conf))
                .def("kernel.port", "9010")
                .def("kernel.tick.ms", "1000")
                .def("kernel.strict", "false")
                .load();

        for (Map.Entry<String, String> e : argv.entrySet()) {
            if (e.getKey().contains(".")) {
                config.set(e.getKey(), e.getValue());
            }
        }

        int port = config.num("kernel.port", 9010);
        boolean strict = config.bool("kernel.strict", false);

        Kernel kernel = new Kernel(config);

        kernel.route("GET", "/", req -> Response.ok(root(config, port)));
        kernel.route("GET", "/api/kernel", req -> Response.ok(kernel.snapshot()));
        kernel.route("GET", "/api/kernel/routes", req -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("count", kernel.routeCount());
            m.put("routes", kernel.routes());
            return Response.ok(m);
        });
        kernel.route("GET", "/api/kernel/config", req -> Response.ok(config.view()));

        // 闸门在入口处装配：传输层只负责调用，不认识模式与令牌。
        Policy policy = new Policy(
                Policy.parse(config.str("security.mode", "readonly")),
                config.str("security.token", ""));
        kernel.route("GET", "/api/security", req -> Response.ok(policy.describe()));

        kernel.register(new LifeSide());
        kernel.register(new RuntimeSide());
        kernel.build().tickLoop();

        if (strict) {
            for (String name : new String[]{"life"}) {
                if (kernel.state(name) == SideState.FAILED) {
                    Log.error("main", "strict=true 且侧面 %s 装配失败，退出码 2", name);
                    kernel.halt();
                    Log.close();
                    System.exit(2);
                }
            }
        }

        HttpService http = new HttpService(port, kernel, policy);
        try {
            http.start();
        } catch (Exception e) {
            // 端口抢不到 = 本进程没有存在意义。不半启动、不占表、不装作活着。
            Log.errorStack("main", "HTTP 启动失败，端口 " + port, e);
            kernel.halt();
            Log.close();
            System.exit(3);
        }

        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            http.stop();
            kernel.halt();
            Log.close();
        }));

        Log.info("main", "就绪：http://0.0.0.0:%d/api/kernel", port);
        new CountDownLatch(1).await();
    }

    private static Map<String, Object> root(Config config, int port) {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("name", "NetOS");
        m.put("version", "3.0.0-repo");
        m.put("port", port);
        m.put("tickMs", config.num("kernel.tick.ms", 1000));
        m.put("strict", config.bool("kernel.strict", false));
        m.put("endpoints", new String[]{
                "GET /", "GET /api/kernel", "GET /api/kernel/routes",
                "GET /api/kernel/config", "GET /api/life", "GET /api/life/host",
                "GET /api/life/heart"
        });
        return m;
    }

    /** --key=value 形式的参数，允许 --port 这类简写。 */
    private static Map<String, String> parse(String[] args) {
        Map<String, String> m = new LinkedHashMap<>();
        for (String a : args) {
            if (!a.startsWith("--")) {
                continue;
            }
            String s = a.substring(2);
            int eq = s.indexOf('=');
            if (eq < 0) {
                m.put(s, "true");
            } else {
                m.put(s.substring(0, eq), s.substring(eq + 1));
            }
        }
        return m;
    }
}