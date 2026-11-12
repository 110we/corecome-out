package astra.compat;

import astra.Astra;
import netos.security.Policy;
import netos.util.Log;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.CountDownLatch;

/**
 * Astra 的兼容入口。
 *
 * 参数名、默认值、日志文案、响应形状，与 netos.Main 一致：
 * 换内核不换运维脚本，才叫无缝。
 */
public final class CompatMain {

    public static void main(String[] args) throws Exception {
        Map<String, String> argv = parse(args);

        int port = Integer.parseInt(argv.getOrDefault("kernel.port", "9010"));
        long tickMs = Long.parseLong(argv.getOrDefault("kernel.tick.ms", "1000"));
        Policy policy = new Policy(
                Policy.parse(argv.get("security.mode")),
                argv.getOrDefault("security.token", ""));

        Astra astra = new Astra();
        astra.install(new LifeFace());

        CompatEdge edge = new CompatEdge(astra, policy, port, tickMs);
        edge.start();

        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            edge.stop();
            Log.info("kernel", "已收摊（astra 内核）");
            Log.close();
        }));

        Log.info("main", "就绪（astra 内核）：http://0.0.0.0:%d/api/kernel", port);
        new CountDownLatch(1).await();
    }

    /** 与 netos.Main 同一个解析器：--key=value，无等号即 true。 */
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