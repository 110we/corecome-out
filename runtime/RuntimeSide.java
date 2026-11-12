package netos.runtime;

import netos.core.Kernel;
import netos.core.Side;
import netos.core.SideState;
import netos.http.Response;
import netos.util.Log;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 运行时侧面 —— 第七面。
 *
 * 前六面（net / security / device / sched / memory / life）是「能力」，
 * 这一面是「实体」：进程表 + 服务表 + 调用入口。
 *
 * 为什么单独成一面而不是塞进 Kernel：
 *   内核只该持有路由、事件、存储、模块表和侧面表，不该认识「进程」这种概念。
 *   把进程放进来，内核就得懂生命周期、懂调度、懂回收 —— 那就不是内核了。
 *   所以进程表属于运行时，运行时是一面，和调度面平级。
 *
 * order = 90：最后装配。这一面要提供「调用其他服务」的能力，
 * 必须等其他面的服务都注册完再起来。
 */
public final class RuntimeSide implements Side {

    private Kernel kernel;
    private SideState state = SideState.NEW;
    private long startedAt;
    private ProcessTable processes;
    private ServiceRegistry services;
    private final AtomicLong ticks = new AtomicLong();
    private final AtomicLong dispatched = new AtomicLong();
    private final AtomicLong reaped = new AtomicLong();

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
        this.state = SideState.INIT;
        this.processes = new ProcessTable(kernel.config().num("runtime.graveyard.max", 256));
        this.services = new ServiceRegistry();

        registerSelfServices();
        registerHttp();

        Log.tag("runtime", "已装配：进程表上限 %d", kernel.config().num("runtime.graveyard.max", 256));
    }

    private void registerSelfServices() {
        services.register("runtime", "ps", "列出活跃进程", in -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("summary", processes.summary());
            m.put("processes", processes.listActive());
            return m;
        });

        services.register("runtime", "graveyard", "列出已终结进程", in -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("processes", processes.listGraveyard((int) num(in, "limit", 50)));
            return m;
        });

        services.register("runtime", "spawn", "创建一个空转进程", in -> {
            String name = str(in, "name", "probe-" + System.nanoTime() % 10000);
            int lifeMs = (int) Math.max(0, Math.min(600000, num(in, "lifeMs", 10000)));
            Process p = processes.create(name, "runtime", () -> {
                try {
                    Thread.sleep(lifeMs);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            });
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("pid", p.pid());
            m.put("name", p.name());
            m.put("lifeMs", lifeMs);
            return m;
        });

        services.register("runtime", "kill", "终结进程", in -> {
            String name = str(in, "name", "");
            long pid = num(in, "pid", -1);
            Process p = pid > 0 ? processes.get(pid) : processes.byName(name);
            boolean ok = false;
            if (p != null && p.isActive()) {
                processes.terminate(p, false, "被 runtime.kill 终结");
                ok = true;
            }
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("ok", ok);
            m.put("killed", p == null ? null : p.pid());
            return m;
        });

        services.register("runtime", "services", "列出所有服务方法", in -> services.overview());

        services.register("runtime", "methods", "列出服务方法明细", in -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("methods", services.listAll());
            return m;
        });

        services.register("runtime", "call", "调用任意已注册服务的方法", in -> {
            String svc = str(in, "service", "");
            String mtd = str(in, "method", "");
            if (svc.isBlank() || mtd.isBlank()) {
                Map<String, Object> bad = new LinkedHashMap<>();
                bad.put("ok", false);
                bad.put("error", "需要 service 与 method");
                return bad;
            }
            Map<String, Object> sub = new LinkedHashMap<>();
            Object args = in.get("args");
            if (args instanceof Map<?, ?> am) {
                for (Map.Entry<?, ?> e : am.entrySet()) {
                    sub.put(String.valueOf(e.getKey()), e.getValue());
                }
            }
            return services.call(svc, mtd, sub);
        });
    }

    private void registerHttp() {
        var r = kernel.router();

        r.get("/api/runtime", req -> Response.ok(status()), "runtime");

        r.get("/api/runtime/ps", req -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("summary", processes.summary());
            m.put("processes", processes.listActive());
            return Response.ok(m);
        }, "runtime");

        r.get("/api/runtime/graveyard", req -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("processes", processes.listGraveyard((int) num(req.query(), "limit", 50)));
            return Response.ok(m);
        }, "runtime");

        r.get("/api/runtime/services", req -> Response.ok(services.overview()), "runtime");

        r.get("/api/runtime/methods", req -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("methods", services.listAll());
            return Response.ok(m);
        }, "runtime");

        r.post("/api/runtime/call", req -> {
            String svc = req.input("service", "");
            String mtd = req.input("method", "");
            if (svc.isBlank() || mtd.isBlank()) {
                return Response.badRequest("需要 service 与 method 参数");
            }
            Map<String, Object> args;
            try {
                args = req.json();
            } catch (Exception e) {
                args = Map.of();
            }
            Map<String, Object> out = services.call(svc, mtd, args);
            boolean success = Boolean.TRUE.equals(out.get("ok"));
            return success ? Response.ok(out)
                    : Response.fail(500, String.valueOf(out.get("error")), 500);
        }, "runtime");

        r.post("/api/runtime/spawn", req -> {
            String name = req.input("name", "");
            int lifeMs = (int) Math.max(0, Math.min(600000, num(req.query(), "lifeMs", 10000)));
            try {
                Process p = processes.create(name.isBlank() ? null : name, "http", () -> {
                    try {
                        Thread.sleep(lifeMs);
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                    }
                });
                return Response.ok(Map.of("pid", p.pid(), "name", p.name(), "lifeMs", lifeMs));
            } catch (IllegalStateException e) {
                return Response.fail(409, e.getMessage(), 409);
            }
        }, "runtime");

        r.delete("/api/runtime/process", req -> {
            String name = req.input("name", "");
            long pid = num(req.query(), "pid", -1);
            Process p = pid > 0 ? processes.get(pid) : processes.byName(name);
            if (p == null || !p.isActive()) {
                return Response.notFound("进程不存在或已终结");
            }
            processes.terminate(p, false, "被 HTTP 请求终结");
            return Response.ok(Map.of("killed", p.pid(), "name", p.name()));
        }, "runtime");
    }

    @Override
    public void start() {
        this.state = SideState.RUNNING;
        this.startedAt = System.currentTimeMillis();
        Process self = processes.create("kernel-self", "kernel", () -> {
        });
        processes.running(self);
        Log.tag("runtime", "已起搏：pid=%d", self.pid());
    }

    @Override
    public void tick(long beat) {
        ticks.incrementAndGet();
        for (Process p : processes.activeAll()) {
            if (p.state() == Process.State.READY) {
                processes.running(p);
                dispatched.incrementAndGet();
            }
        }
        int n = processes.reap();
        if (n > 0) {
            reaped.addAndGet(n);
        }
    }

    @Override
    public void stop() {
        this.state = SideState.STOPPED;
        int n = processes.terminateAll(false, "内核收束");
        services.unregisterService("runtime");
        Log.tag("runtime", "已收束，终结 %d 个进程", n);
    }

    @Override
    public Map<String, Object> snapshot() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("state", state.name());
        m.put("processes", processes == null ? Map.of() : processes.summary());
        m.put("services", services == null ? 0 : services.serviceNames().size());
        m.put("methods", services == null ? 0 : services.methodCount());
        m.put("dispatched", dispatched.get());
        m.put("reaped", reaped.get());
        m.put("runningMs", startedAt == 0 ? 0 : System.currentTimeMillis() - startedAt);
        m.put("ticks", ticks.get());
        return m;
    }

    public ProcessTable processes() {
        return processes;
    }

    public ServiceRegistry services() {
        return services;
    }

    public Map<String, Object> status() {
        Map<String, Object> m = new LinkedHashMap<>(snapshot());
        m.put("javaVersion", System.getProperty("java.version"));
        m.put("osName", System.getProperty("os.name"));
        m.put("availableProcessors", Runtime.getRuntime().availableProcessors());
        m.put("heapUsedMb", (Runtime.getRuntime().totalMemory() - Runtime.getRuntime().freeMemory()) / 1048576L);
        m.put("heapMaxMb", Runtime.getRuntime().maxMemory() / 1048576L);
        return m;
    }

    static String str(Map<String, Object> in, String key, String def) {
        if (in == null) {
            return def;
        }
        Object v = in.get(key);
        return v == null ? def : String.valueOf(v);
    }

    static long num(Map<String, Object> in, String key, long def) {
        if (in == null) {
            return def;
        }
        Object v = in.get(key);
        if (v == null) {
            return def;
        }
        if (v instanceof Number n) {
            return n.longValue();
        }
        try {
            return Long.parseLong(String.valueOf(v).trim());
        } catch (RuntimeException e) {
            return def;
        }
    }
}