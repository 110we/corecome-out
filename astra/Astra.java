package astra;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 内核装配入口：签发能力、安装模块、驱动心跳、兑现请求的答复。
 *
 * 唯一的 Cap 签发点。撤销 = 从表里摘掉，已发出去的那张纸立刻作废。
 */
public final class Astra {

    private final Bus bus;
    private final Map<String, Cap> caps = new ConcurrentHashMap<>();
    private final List<Module> modules = new ArrayList<>();
    private final Map<String, CompletableFuture<Map<String, Object>>> pending =
            new ConcurrentHashMap<>();
    private final AtomicLong calls = new AtomicLong();
    private final Cap internal;
    private long ticks;

    public Astra() {
        this(4096, 256);
    }

    public Astra(int maxInbox, int drainLimit) {
        // 内核自用能力：只够投递答复，不能替任何人发业务消息。
        this.internal = Cap.issue("kernel.internal", "astra.reply",
                Set.of(Cap.Right.SEND, Cap.Right.RECV), 0);
        this.bus = new Bus(maxInbox, drainLimit, internal);
        install(new ReplySink());
    }

    public Cap issue(String id, String pattern, Cap.Right... rights) {
        Cap cap = Cap.issue(id, pattern, Set.of(rights), 0);
        caps.put(id, cap);
        return cap;
    }

    public void install(Module module) {
        modules.add(module);
        for (String pattern : module.subscribes()) {
            bus.subscribe(pattern, module);
        }
    }

    public void boot() {
        // 装配与运行分开：测试要能卡在中间观察。
    }

    public int tick() {
        ticks++;
        return bus.tick();
    }

    /**
     * 请求-答复：发一条消息，等它回一条。
     * 返回 null 表示没能投递，或超时没等到答复——两种都算失败，不猜。
     */
    public Map<String, Object> request(String from, String topic, String command,
                                       Map<String, Object> payload, Cap cap, long timeoutMs) {
        String corr = "r" + calls.incrementAndGet() + "-" + System.nanoTime();
        CompletableFuture<Map<String, Object>> future = new CompletableFuture<>();
        pending.put(corr, future);
        Map<String, Object> p = new LinkedHashMap<>(payload);
        p.put("corr", corr);
        if (!bus.send(Message.of(from, topic, command, p, cap)).ok()) {
            pending.remove(corr);
            return null;
        }
        try {
            return future.get(timeoutMs, TimeUnit.MILLISECONDS);
        } catch (Exception e) {
            return null;
        } finally {
            pending.remove(corr);
        }
    }

    void complete(String corr, Map<String, Object> data) {
        CompletableFuture<Map<String, Object>> f = pending.get(corr);
        if (f != null) {
            f.complete(data);
        }
    }

    public Cap cap(String id) {
        return id == null ? null : caps.get(id);
    }

    public boolean revoke(String id) {
        return caps.remove(id) != null;
    }

    public Bus bus() {
        return bus;
    }

    public Map<String, Object> health() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("kernel", "astra");
        m.put("ticks", ticks);
        m.put("modules", modules.stream().map(Module::name).sorted().toList());
        m.put("caps", caps.size());
        m.put("inflight", pending.size());
        m.putAll(bus.health());
        return m;
    }

    /** 收答复。名字里带 sink，因为它只往下沉，不做什么判断。 */
    private final class ReplySink implements Module {

        @Override
        public String name() {
            return "reply-sink";
        }

        @Override
        public String[] subscribes() {
            return new String[]{"astra.reply"};
        }

        @Override
        public void onMessage(Message m, Bus b) {
            complete(String.valueOf(m.payload().getOrDefault("corr", "")), m.payload());
        }
    }
}