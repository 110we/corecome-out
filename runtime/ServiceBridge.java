package netos.runtime;

import netos.core.Kernel;
import netos.core.Side;
import netos.util.Log;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * 服务桥接器。
 *
 * 职责只有一个：把已装配的各个侧面，翻译成 ServiceRegistry 里的可调用方法。
 *
 * 为什么不在每个侧面里写 registerServices()？
 *   侧面不该认识「服务」这个概念 —— 那是运行时的词汇。
 *   让 SchedSide 去 import netos.runtime.ServiceRegistry，等于让能力和承载它的
 *   实体反向耦合。侧面只该提供干净的方法，桥接是运行时的事。
 *
 * 所以这里全部用反射之外的方式：直接拿接口、直接调方法。
 * 编译期就能发现签名错误，比反射安全得多。
 */
public final class ServiceBridge {

    private final Kernel kernel;
    private final ServiceRegistry services;
    private int registered = 0;

    public ServiceBridge(Kernel kernel, ServiceRegistry services) {
        this.kernel = kernel;
        this.services = services;
    }

    public int registered() {
        return registered;
    }

    /**
     * 全量桥接。按侧面 order 顺序执行，后注册的覆盖先注册的（同服务同方法）。
     */
    public int bridgeAll() {
        bridgeSched();
        bridgeDevice();
        bridgeMemory();
        bridgeSecurity();
        bridgeKernel();
        Log.tag("runtime", "服务桥接完成：%d 个方法", registered);
        return registered;
    }

    /* ==================== sched ==================== */

    private void bridgeSched() {
        Side s = kernel.side("sched");
        if (!(s instanceof netos.sched.SchedSide sch)) {
            return;
        }
        reg("sched", "status", "调度队列与计数器", in -> sch.status());
        reg("sched", "submit", "提交一个内置任务(sleep|fail)", in -> {
            String name = RuntimeSide.str(in, "name", "job-" + System.nanoTime() % 100000);
            String kind = RuntimeSide.str(in, "kind", "sleep");
            int ms = (int) Math.max(0, Math.min(60000, RuntimeSide.num(in, "ms", 1000)));
            boolean ok = sch.submit(name, () -> {
                switch (kind) {
                    case "sleep" -> {
                        try {
                            Thread.sleep(ms);
                        } catch (InterruptedException e) {
                            Thread.currentThread().interrupt();
                        }
                    }
                    case "fail" -> throw new IllegalStateException("被要求的失败: " + name);
                    default -> { }
                }
            });
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("ok", ok);
            m.put("name", name);
            m.put("kind", kind);
            m.put("ms", ms);
            return m;
        });
        reg("sched", "drain", "清空队列", in -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("drained", sch.drain());
            return m;
        });
    }

    /* ==================== device ==================== */

    private void bridgeDevice() {
        Side s = kernel.side("device");
        if (!(s instanceof netos.device.DeviceSide dev)) {
            return;
        }
        reg("device", "report", "设备全量报告(缓存)", in -> dev.report());
        reg("device", "scan", "强制重新扫描", in -> dev.scan());
        reg("device", "cpu", "CPU 信息", in -> dev.cpu());
        reg("device", "memory", "内存信息", in -> dev.memory());
        reg("device", "disk", "磁盘列表", in -> {
            Map<String, Object> m = new LinkedHashMap<>();
            List<Map<String, Object>> d = dev.disk();
            m.put("disks", d);
            m.put("count", d == null ? 0 : d.size());
            return m;
        });
        reg("device", "snapshot", "侧面快照", in -> dev.snapshot());
    }

    /* ==================== memory ==================== */

    private void bridgeMemory() {
        Side s = kernel.side("memory");
        if (!(s instanceof netos.memory.MemorySide mem)) {
            return;
        }
        reg("memory", "snapshot", "内存侧面快照", in -> mem.snapshot());
        reg("memory", "mark", "标记一次事件", in -> {
            String key = RuntimeSide.str(in, "key", "");
            String note = RuntimeSide.str(in, "note", "");
            if (key.isBlank()) {
                Map<String, Object> bad = new LinkedHashMap<>();
                bad.put("ok", false);
                bad.put("error", "需要 key");
                return bad;
            }
            if (note.isBlank()) {
                mem.mark(key);
            } else {
                mem.mark(key, note);
            }
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("key", key);
            m.put("count", mem.count(key));
            return m;
        });
        reg("memory", "count", "读事件计数", in -> {
            String key = RuntimeSide.str(in, "key", "");
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("key", key);
            m.put("count", mem.count(key));
            return m;
        });
        reg("memory", "add", "增量计数", in -> {
            String key = RuntimeSide.str(in, "key", "");
            long d = RuntimeSide.num(in, "delta", 1);
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("key", key);
            m.put("value", mem.add(key, d));
            return m;
        });
    }

    /* ==================== security ==================== */

    private void bridgeSecurity() {
        Side s = kernel.side("security");
        if (!(s instanceof netos.security.SecuritySide sec)) {
            return;
        }
        reg("security", "snapshot", "安全侧面快照", in -> sec.snapshot());
        reg("security", "issue", "签发令牌", in -> sec.issue(RuntimeSide.str(in, "who", "anyone")));
        reg("security", "verify", "校验令牌", in -> {
            String token = RuntimeSide.str(in, "token", "");
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("token", token);
            m.put("valid", sec.verify(token));
            return m;
        });
    }

    /* ==================== kernel ==================== */

    private void bridgeKernel() {
        reg("kernel", "info", "内核自述", in -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("name", "NetOS");
            m.put("version", "3.0.0");
            m.put("codename", "pure");
            m.put("home", String.valueOf(kernel.home()));
            m.put("sides", kernel.describeSides());
            m.put("alive", kernel.aliveSides());
            return m;
        });
        reg("kernel", "sides", "侧面清单", in -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("sides", kernel.describeSides());
            return m;
        });
        reg("kernel", "events", "事件总线统计", in -> {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("info", kernel.events() == null ? Map.of() : kernel.events().info());
            return m;
        });
    }

    /* ==================== 工具 ==================== */

    private void reg(String service, String method, String desc, ServiceRegistry.Handler h) {
        services.register(service, method, desc, h);
        registered++;
    }
}