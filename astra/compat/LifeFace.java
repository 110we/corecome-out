package astra.compat;

import astra.Bus;
import astra.Message;
import astra.Module;

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
 * life 面的等价实现。字段与 netos.sides.LifeSide 逐一对齐——
 * 对接方看不到差别，才叫兼容；"差不多"最后都会变成对不上。
 */
public final class LifeFace implements Module {

    private final AtomicLong beats = new AtomicLong();
    private final long born = System.currentTimeMillis();

    @Override
    public String name() {
        return "life";
    }

    @Override
    public String[] subscribes() {
        return new String[]{"life.*"};
    }

    @Override
    public void onMessage(Message m, Bus bus) {
        beats.incrementAndGet();
        if ("tick".equals(m.command())) {
            return; // 拍子只记账，不答复
        }
        Map<String, Object> data = switch (m.command()) {
            case "info" -> info();
            case "host" -> host();
            case "heart" -> Map.of("beats", beats.get());
            case "poke" -> poke(m);
            default -> Map.of("error", "unknown command: " + m.command());
        };
        bus.reply(m, data);
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

    private Map<String, Object> host() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("hostname", hostname());
        m.put("java", System.getProperty("java.version"));
        m.put("pid", ManagementFactory.getRuntimeMXBean().getName());
        m.put("uptimeMs", ManagementFactory.getRuntimeMXBean().getUptime());
        return m;
    }

    private Map<String, Object> poke(Message m) {
        Map<String, Object> p = m.payload();
        Map<String, Object> out = new LinkedHashMap<>();
        out.put("ok", true);
        out.put("from", String.valueOf(p.getOrDefault("from", "-")));
        out.put("bodyLen", p.getOrDefault("bodyLen", 0));
        out.put("beats", beats.get());
        return out;
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