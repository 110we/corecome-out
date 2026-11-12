package netos.sides;

import netos.core.Kernel;
import netos.core.Side;
import netos.core.SideState;
import netos.http.Response;

import java.io.IOException;
import java.lang.management.ManagementFactory;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 样例面 · life。
 *
 * 面不是"功能集合"，是"可独立失败的一块"。这一面同时当三个例子：
 *   1. 怎么注册路由（init 里 route）
 *   2. 怎么活（tick 里记账，不做重活）
 *   3. 怎么被观测（snapshot 返回可读的 Map）
 *
 * order = 60：排在 runtime(90) 前面，因为它不需要调用别人。
 */
public final class LifeSide implements Side {

    private final AtomicLong beats = new AtomicLong();
    private final long born = System.currentTimeMillis();
    private SideState state = SideState.NEW;

    @Override
    public String name() {
        return "life";
    }

    @Override
    public int order() {
        return 60;
    }

    @Override
    public void init(Kernel kernel) {
        state = SideState.INIT;
        kernel.route("GET", "/api/life", req -> Response.ok(info()));
        kernel.route("GET", "/api/life/host", req -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("hostname", hostname());
            m.put("java", System.getProperty("java.version"));
            m.put("pid", ManagementFactory.getRuntimeMXBean().getName());
            m.put("uptimeMs", ManagementFactory.getRuntimeMXBean().getUptime());
            return Response.ok(m);
        });
        kernel.route("GET", "/api/life/heart", req -> Response.ok(Map.of("beats", beats.get())));

        // 写面样例：用来验证闸门在「非 GET」上真的会拦。
        kernel.route("POST", "/api/life/poke", req -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("ok", true);
            m.put("from", req.clientIp());
            m.put("bodyLen", req.body().length());
            m.put("beats", beats.get());
            return Response.ok(m);
        });
    }

    @Override
    public void start() {
        state = SideState.RUNNING;
    }

    @Override
    public void tick() {
        beats.incrementAndGet();
    }

    @Override
    public Map<String, Object> snapshot() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("beats", beats.get());
        m.put("state", state.name());
        m.put("bornAt", born);
        return m;
    }

    private Map<String, Object> info() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("side", "life");
        m.put("order", 60);
        m.put("beats", beats.get());
        m.put("ageMs", System.currentTimeMillis() - born);
        m.put("hostname", hostname());
        m.put("note", "这一面只证明内核在跳；真实的 hostname/mounts 能力在服务器版 init 面");
        return m;
    }

    private static String hostname() {
        try {
            List<String> lines = Files.readAllLines(Path.of("/etc/hostname"), StandardCharsets.UTF_8);
            return lines.isEmpty() ? "unknown" : lines.get(0).trim();
        } catch (IOException e) {
            return "unknown";
        }
    }
}