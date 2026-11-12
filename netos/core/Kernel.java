package netos.core;

import netos.http.Handler;
import netos.http.Request;
import netos.http.Response;
import netos.util.Log;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 内核。
 *
 * 只做四件事：装侧面、存路由、发拍子、收摊。
 * 不认识进程，不认识 HTTP，不认识安全策略 —— 那些都是侧面的事。
 * 内核一旦认识其中任何一个，就会被它的细节绑住，
 * 从此每加一个能力都要改内核：那不是内核，是上帝对象。
 */
public final class Kernel {

    private final Config config;
    private final Map<String, Side> sides = new LinkedHashMap<>();
    private final Map<String, SideState> states = new ConcurrentHashMap<>();
    private final Map<String, Handler> routes = new ConcurrentHashMap<>();

    private final AtomicLong ticks = new AtomicLong();
    private final long startedAt = System.currentTimeMillis();

    private ScheduledExecutorService loop;
    private volatile boolean running;

    public Kernel(Config config) {
        this.config = config;
    }

    /* ==================== 装配 ==================== */

    public Kernel register(Side side) {
        if (sides.containsKey(side.name())) {
            throw new IllegalStateException("侧面重名: " + side.name());
        }
        sides.put(side.name(), side);
        states.put(side.name(), SideState.NEW);
        return this;
    }

    /**
     * 先全部 init，再全部 start。任一失败只标 FAILED，不中断 —— 缺腿能站。
     * 注意是两轮而不是一轮：后面装配的面不应该看到前面"半起跑"的状态。
     */
    public Kernel build() {
        List<Side> ordered = ordered();
        for (Side s : ordered) {
            states.put(s.name(), SideState.INIT);
            try {
                s.init(this);
            } catch (Throwable t) {
                states.put(s.name(), SideState.FAILED);
                Log.errorStack("kernel", "侧面 " + s.name() + " 装配失败，继续", t);
            }
        }
        for (Side s : ordered) {
            if (states.get(s.name()) != SideState.INIT) {
                continue;
            }
            try {
                s.start();
                states.put(s.name(), SideState.RUNNING);
            } catch (Throwable t) {
                states.put(s.name(), SideState.FAILED);
                Log.errorStack("kernel", "侧面 " + s.name() + " 起跑失败，继续", t);
            }
        }
        long up = ordered.stream().filter(s -> states.get(s.name()) == SideState.RUNNING).count();
        Log.info("kernel", "装配完成：%d/%d 面存活，%d 条路由",
                (int) up, ordered.size(), routes.size());
        return this;
    }

    /** 起拍子。单线程，拍内串行，任何一面慢都会拖慢整拍 —— 这是刻意的。 */
    public Kernel tickLoop() {
        int ms = Math.max(50, config.num("kernel.tick.ms", 1000));
        loop = Executors.newSingleThreadScheduledExecutor(r -> {
            Thread t = new Thread(r, "netos-tick");
            t.setDaemon(true);
            return t;
        });
        loop.scheduleWithFixedDelay(() -> {
            ticks.incrementAndGet();
            for (Side s : sides.values()) {
                if (states.get(s.name()) != SideState.RUNNING) {
                    continue;
                }
                try {
                    s.tick();
                } catch (Throwable t) {
                    Log.warn("kernel", "侧面 %s tick 异常：%s", s.name(), t.getMessage());
                }
            }
        }, ms, ms, TimeUnit.MILLISECONDS);
        running = true;
        Log.info("kernel", "拍子已起：%d ms/拍", ms);
        return this;
    }

    /* ==================== 路由 ==================== */

    public void route(String method, String path, Handler handler) {
        String key = method.toUpperCase() + " " + path;
        if (routes.putIfAbsent(key, handler) != null) {
            throw new IllegalStateException("路由重复注册: " + key);
        }
    }

    public Response dispatch(Request req) throws Exception {
        Handler h = routes.get(req.method().toUpperCase() + " " + req.path());
        if (h == null) {
            return Response.notfound("无此路由: " + req.method() + " " + req.path());
        }
        return h.handle(req);
    }

    public int routeCount() {
        return routes.size();
    }

    public List<String> routes() {
        List<String> out = new ArrayList<>(routes.keySet());
        out.sort(String::compareTo);
        return out;
    }

    /* ==================== 观测 ==================== */

    public Config config() {
        return config;
    }

    public SideState state(String name) {
        return states.getOrDefault(name, SideState.NEW);
    }

    public boolean isRunning() {
        return running;
    }

    public Map<String, Object> snapshot() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("sides", sideSnapshots());
        m.put("alive", sides.values().stream()
                .filter(s -> states.get(s.name()) == SideState.RUNNING).count());
        m.put("total", sides.size());
        m.put("routes", routes.size());
        m.put("ticks", ticks.get());
        m.put("uptimeMs", System.currentTimeMillis() - startedAt);
        m.put("tickMs", config.num("kernel.tick.ms", 1000));
        return m;
    }

    private List<Map<String, Object>> sideSnapshots() {
        List<Map<String, Object>> list = new ArrayList<>();
        for (Side s : ordered()) {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("name", s.name());
            m.put("order", s.order());
            m.put("state", states.get(s.name()).name());
            try {
                m.put("detail", s.snapshot());
            } catch (Throwable t) {
                m.put("detail", "snapshot 失败: " + t.getMessage());
            }
            list.add(m);
        }
        return list;
    }

    private List<Side> ordered() {
        List<Side> list = new ArrayList<>(sides.values());
        list.sort(Comparator.comparingInt(Side::order).thenComparing(Side::name));
        return list;
    }

    /* ==================== 收摊 ==================== */

    public void halt() {
        running = false;
        if (loop != null) {
            loop.shutdownNow();
        }
        List<Side> reversed = new ArrayList<>(ordered());
        reversed.sort(Comparator.comparingInt(Side::order).reversed());
        for (Side s : reversed) {
            if (states.get(s.name()) != SideState.RUNNING) {
                continue;
            }
            try {
                s.stop();
                states.put(s.name(), SideState.STOPPED);
            } catch (Throwable t) {
                Log.errorStack("kernel", "侧面 " + s.name() + " 收摊异常", t);
            }
        }
        Log.info("kernel", "已收摊：%d 条路由、%d 拍", routes.size(), ticks.get());
    }
}