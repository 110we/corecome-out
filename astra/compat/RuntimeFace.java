package astra.compat;

import java.lang.management.ManagementFactory;
import java.lang.management.MemoryUsage;
import java.lang.management.ThreadMXBean;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.concurrent.atomic.AtomicLong;

/**
 * runtime 观测面。
 *
 * 数据源是 CompatEdge 里那张真实的 HTTP 路由表——不是伪造一份 /proc。
 * 服务活着 = 路由还在；掉线只标记，不删。
 */
public final class RuntimeFace {

    private static final int HISTORY = 60;

    private final Map<String, Service> services = new TreeMap<>();
    private final Deque<Map<String, Object>> history = new ArrayDeque<>();
    private final AtomicLong samples = new AtomicLong();
    private final long born = System.currentTimeMillis();
    private final List<String> routes;

    private static final class Service {
        final String key;
        final long firstSeen = System.currentTimeMillis();
        long lastSeen = firstSeen;
        long ticks;
        boolean alive = true;

        Service(String key) {
            this.key = key;
        }
    }

    public RuntimeFace(List<String> routes) {
        this.routes = List.copyOf(routes);
        refresh();
    }

    /** 每拍调一次：刷新服务表并留一格历史。 */
    public void beat() {
        refresh();
        Map<String, Object> snap = new LinkedHashMap<>();
        snap.put("t", System.currentTimeMillis());
        snap.put("routes", routes.size());
        snap.put("heapUsed", used());
        snap.put("threads", threads().get("count"));
        snap.put("samples", samples.incrementAndGet());
        synchronized (history) {
            history.addLast(snap);
            while (history.size() > HISTORY) {
                history.removeFirst();
            }
        }
    }

    private void refresh() {
        for (String key : routes) {
            Service s = services.computeIfAbsent(key, Service::new);
            s.lastSeen = System.currentTimeMillis();
            s.ticks++;
            s.alive = true;
        }
        services.values().forEach(s -> {
            if (!routes.contains(s.key)) {
                s.alive = false;
            }
        });
    }

    public Map<String, Object> api() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("side", "runtime");
        m.put("order", 90);
        m.put("services", services.size());
        m.put("deadServices", services.values().stream().filter(s -> !s.alive).count());
        m.put("samples", samples.get());
        m.put("ageMs", System.currentTimeMillis() - born);
        m.put("jvm", jvm());
        return m;
    }

    public Map<String, Object> services() {
        List<Map<String, Object>> list = new ArrayList<>();
        services.values().forEach(s -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("key", s.key);
            m.put("alive", s.alive);
            m.put("ticks", s.ticks);
            m.put("firstSeen", s.firstSeen);
            m.put("lastSeen", s.lastSeen);
            list.add(m);
        });
        Map<String, Object> out = new LinkedHashMap<>();
        out.put("count", list.size());
        out.put("services", list);
        return out;
    }

    public Map<String, Object> history() {
        Map<String, Object> out = new LinkedHashMap<>();
        out.put("capacity", HISTORY);
        synchronized (history) {
            out.put("samples", history.size());
            out.put("history", new ArrayList<>(history));
        }
        return out;
    }

    public static Map<String, Object> jvm() {
        MemoryUsage heap = ManagementFactory.getMemoryMXBean().getHeapMemoryUsage();
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("heapUsed", heap.getUsed());
        m.put("heapMax", heap.getMax());
        m.put("heapCommitted", heap.getCommitted());
        m.put("threads", threads());
        m.put("uptimeMs", ManagementFactory.getRuntimeMXBean().getUptime());
        m.put("gc", ManagementFactory.getGarbageCollectorMXBeans().stream()
                .mapToLong(g -> g.getCollectionCount()).sum());
        m.put("cpu", ManagementFactory.getOperatingSystemMXBean().getSystemLoadAverage());
        return m;
    }

    private static Map<String, Object> threads() {
        ThreadMXBean t = ManagementFactory.getThreadMXBean();
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("count", t.getThreadCount());
        m.put("peak", t.getPeakThreadCount());
        m.put("daemon", t.getDaemonThreadCount());
        m.put("totalStarted", t.getTotalStartedThreadCount());
        return m;
    }

    private static long used() {
        return ManagementFactory.getMemoryMXBean().getHeapMemoryUsage().getUsed();
    }
}