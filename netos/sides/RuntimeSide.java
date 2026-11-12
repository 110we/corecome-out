package netos.sides;

import netos.core.Kernel;
import netos.core.Side;
import netos.core.SideState;
import netos.http.Response;

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
 * 第七面 · runtime。
 *
 * 这一面不"实现"进程，只如实反映进程里正在发生什么：
 *   - 服务表：每拍从内核路由表刷新，记录首次出现、存活拍数、消失时刻
 *   - JVM：堆、线程、GC
 *   - 历史：环形缓冲，最近 60 拍
 *
 * 为什么不做假的 /proc 扫描：这层跑在 JVM 里，抄一份 /proc 出来只是自欺。
 * 真实的进程表在服务器版的 ProcessTable，用 OS pid 说话；这里先把「可观测」做实。
 *
 * order = 90：最后起、最先停，因为它依赖别人先注册完路由。
 */
public final class RuntimeSide implements Side {

    private static final int HISTORY = 60;

    private final Map<String, Service> services = new TreeMap<>();
    private final Deque<Map<String, Object>> history = new ArrayDeque<>();
    private final AtomicLong samples = new AtomicLong();
    private final long born = System.currentTimeMillis();

    private Kernel kernel;
    private SideState state = SideState.NEW;

    /** 一个服务就是一条路由。生卒写清楚，才知道它是新长的还是回魂的。 */
    private static final class Service {
        final String key;
        long firstSeen;
        long lastSeen;
        long ticks;
        boolean alive = true;

        Service(String key, long now) {
            this.key = key;
            this.firstSeen = now;
            this.lastSeen = now;
        }
    }

    @Override
    public String name() {
        return "runtime";
    }

    @Override
    public int order() {
        return 90;
    }

    @Override
    public void init(Kernel kernel) {
        this.kernel = kernel;
        state = SideState.INIT;

        kernel.route("GET", "/api/runtime", req -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("side", "runtime");
            m.put("order", 90);
            m.put("services", services.size());
            m.put("deadServices", services.values().stream().filter(s -> !s.alive).count());
            m.put("samples", samples.get());
            m.put("ageMs", System.currentTimeMillis() - born);
            m.put("jvm", jvm());
            return Response.ok(m);
        });

        kernel.route("GET", "/api/runtime/services", req -> {
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
            return Response.ok(out);
        });

        kernel.route("GET", "/api/runtime/jvm", req -> Response.ok(jvm()));

        kernel.route("GET", "/api/runtime/history", req -> {
            Map<String, Object> out = new LinkedHashMap<>();
            out.put("capacity", HISTORY);
            out.put("samples", history.size());
            out.put("history", new ArrayList<>(history));
            return Response.ok(out);
        });

        // 装配完先刷一次：第一拍之前也可能有人来查。
        refresh();
    }

    @Override
    public void start() {
        state = SideState.RUNNING;
    }

    @Override
    public void tick() {
        long now = System.currentTimeMillis();
        Map<String, Object> snap = new LinkedHashMap<>();

        // 路由表是唯一真相：服务活着 = 路由还在。
        refresh();
        List<String> current = kernel.routes();

        snap.put("t", now);
        snap.put("routes", current.size());
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

    /** 刷新服务表：装配完与每一拍都走这里，只有一处真相。 */
    private void refresh() {
        long now = System.currentTimeMillis();
        List<String> current = kernel.routes();
        for (String key : current) {
            Service s = services.get(key);
            if (s == null) {
                s = new Service(key, now);
                services.put(key, s);
            }
            s.lastSeen = now;
            s.ticks++;
            s.alive = true;
        }
        // 掉线标记：不删。删了就没人知道它曾经存在过。
        services.values().forEach(s -> {
            if (!current.contains(s.key)) {
                s.alive = false;
            }
        });
    }

    @Override
    public Map<String, Object> snapshot() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("state", state.name());
        m.put("services", services.size());
        m.put("samples", samples.get());
        m.put("historyDepth", history.size());
        return m;
    }

    private static long used() {
        MemoryUsage heap = ManagementFactory.getMemoryMXBean().getHeapMemoryUsage();
        return heap.getUsed();
    }

    private static Map<String, Object> jvm() {
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
}