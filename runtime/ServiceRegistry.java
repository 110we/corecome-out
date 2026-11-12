package netos.runtime;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 服务注册表。
 *
 * 「服务」与「进程」的区别在这里划清：
 *   进程 = 有生命周期的执行体（会开始、会结束、会失败）
 *   服务 = 有名字、有能力、可以被别人查找和调用的东西（不关心谁在跑）
 *
 * 一个服务可能由若干进程支撑，也可能什么都不跑只是暴露一组方法。
 * 这个区分很重要，因为内核里绝大多数东西其实是服务，不是进程：
 * 设备扫描、存储、安全校验、任务调度 —— 它们被调用，而不是「在跑」。
 *
 * 服务统一用 Handler 表示：入参 Map，出参 Map。
 * 不做泛型、不做接口生成 —— 那种优雅需要更多代码和更强的契约，
 * 而这里的首要目标是「任何模块都能注册任何能力，且能被 HTTP 直接调用」。
 */
public final class ServiceRegistry {

    /** 服务方法：入参 -> 出参 */
    public interface Handler {
        Map<String, Object> call(Map<String, Object> in) throws Exception;
    }

    /** 一个可调用的服务方法 */
    public static final class Method {
        final String service;
        final String name;
        final String desc;
        final Handler handler;
        final AtomicLong calls = new AtomicLong();
        final AtomicLong errors = new AtomicLong();
        final AtomicLong totalMs = new AtomicLong();
        volatile long lastCallAt;

        Method(String service, String name, String desc, Handler handler) {
            this.service = service;
            this.name = name;
            this.desc = desc;
            this.handler = handler;
        }

        public Map<String, Object> view() {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("service", service);
            m.put("method", name);
            m.put("desc", desc);
            m.put("calls", calls.get());
            m.put("errors", errors.get());
            m.put("avgMs", calls.get() == 0 ? 0 : totalMs.get() / calls.get());
            m.put("lastCallAt", lastCallAt);
            return m;
        }
    }

    /** service 名 -> (method 名 -> Method) */
    private final Map<String, Map<String, Method>> services = new ConcurrentHashMap<>();

    /* ==================== 注册 ==================== */

    /**
     * 注册一个服务方法。同名同服务会覆盖 —— 这允许热修复单个方法。
     */
    public Method register(String service, String method, String desc, Handler handler) {
        if (service == null || service.isBlank() || method == null || method.isBlank() || handler == null) {
            throw new IllegalArgumentException("服务名、方法名、处理器都不能为空");
        }
        Method h = new Method(service.trim(), method.trim(),
                desc == null ? "" : desc, handler);
        services.computeIfAbsent(h.service, k -> new ConcurrentHashMap<>())
                .put(h.name, h);
        return h;
    }

    /**
     * 一次性注册一个服务的多个方法。
     */
    public void registerAll(String service, Map<String, Handler> methods) {
        if (methods == null) {
            return;
        }
        for (Map.Entry<String, Handler> e : methods.entrySet()) {
            register(service, e.getKey(), "", e.getValue());
        }
    }

    public void unregister(String service, String method) {
        Map<String, Method> m = services.get(service);
        if (m != null) {
            m.remove(method);
            if (m.isEmpty()) {
                services.remove(service);
            }
        }
    }

    public int unregisterService(String service) {
        Map<String, Method> m = services.remove(service);
        return m == null ? 0 : m.size();
    }

    /* ==================== 调用 ==================== */

    /**
     * 调用服务方法。
     *
     * 任何异常都被捕获并转成错误 Map —— 服务不应该能把调用方拖垮。
     */
    public Map<String, Object> call(String service, String method, Map<String, Object> in) {
        Map<String, Object> err = new LinkedHashMap<>();
        Map<String, Method> m = services.get(service);
        if (m == null) {
            err.put("ok", false);
            err.put("error", "服务不存在: " + service);
            return err;
        }
        Method h = m.get(method);
        if (h == null) {
            err.put("ok", false);
            err.put("error", "方法不存在: " + service + "." + method);
            return err;
        }
        long t0 = System.currentTimeMillis();
        h.calls.incrementAndGet();
        h.lastCallAt = t0;
        try {
            Map<String, Object> out = h.handler.call(in == null ? Map.of() : in);
            Map<String, Object> res = out == null ? new LinkedHashMap<>() : new LinkedHashMap<>(out);
            res.putIfAbsent("ok", true);
            return res;
        } catch (Exception e) {
            h.errors.incrementAndGet();
            err.put("ok", false);
            err.put("error", e.getClass().getSimpleName() + ": " + e.getMessage());
            return err;
        } finally {
            h.totalMs.addAndGet(System.currentTimeMillis() - t0);
        }
    }

    /* ==================== 查询 ==================== */

    public boolean has(String service, String method) {
        Map<String, Method> m = services.get(service);
        return m != null && m.containsKey(method);
    }

    public boolean hasService(String service) {
        return services.containsKey(service);
    }

    public Method get(String service, String method) {
        Map<String, Method> m = services.get(service);
        return m == null ? null : m.get(method);
    }

    public List<String> serviceNames() {
        List<String> names = new ArrayList<>(services.keySet());
        names.sort(String::compareTo);
        return names;
    }

    public int methodCount() {
        int n = 0;
        for (Map<String, Method> m : services.values()) {
            n += m.size();
        }
        return n;
    }

    /** 全部方法清单，按服务名 + 方法名排序 */
    public List<Map<String, Object>> listAll() {
        List<Map<String, Object>> out = new ArrayList<>();
        List<String> sn = serviceNames();
        for (String s : sn) {
            Map<String, Method> m = services.get(s);
            if (m == null) {
                continue;
            }
            List<String> mn = new ArrayList<>(m.keySet());
            mn.sort(String::compareTo);
            for (String name : mn) {
                out.add(m.get(name).view());
            }
        }
        return out;
    }

    /** 按服务名分组的概览 */
    public Map<String, Object> overview() {
        Map<String, Object> out = new LinkedHashMap<>();
        out.put("services", services.size());
        out.put("methods", methodCount());
        Map<String, Object> detail = new LinkedHashMap<>();
        for (String s : serviceNames()) {
            Map<String, Method> m = services.get(s);
            List<String> names = m == null ? List.of() : new ArrayList<>(m.keySet());
            names.sort(String::compareTo);
            detail.put(s, names);
        }
        out.put("detail", detail);
        return out;
    }
}