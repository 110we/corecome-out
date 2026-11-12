package netos.core;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * 配置。
 *
 * 优先级：命令行（-D key=value）> 文件（conf/kernel.conf）> 默认值。
 * 配置是骨骼，运行期不改形 —— 改完必须重启。这条不是洁癖：
 * 允许热改的东西一旦多了，"现在到底是什么配置"就没人答得上来。
 */
public final class Config {

    private final Map<String, String> values = new LinkedHashMap<>();
    private final Map<String, String> defaults = new LinkedHashMap<>();
    private final Path file;

    public Config(Path file) {
        this.file = file;
    }

    /** 登记默认值；同名已存在则不改（文件优先于默认）。 */
    public Config def(String key, String value) {
        defaults.putIfAbsent(key, value);
        return this;
    }

    /** 读文件。文件不存在不算错 —— 全靠默认值也能起。 */
    public Config load() throws IOException {
        if (file != null && Files.isRegularFile(file)) {
            for (String raw : Files.readAllLines(file, StandardCharsets.UTF_8)) {
                String line = raw.trim();
                if (line.isEmpty() || line.startsWith("#") || line.startsWith(";")) {
                    continue;
                }
                int eq = line.indexOf('=');
                if (eq <= 0) {
                    continue;
                }
                values.put(line.substring(0, eq).trim(), line.substring(eq + 1).trim());
            }
        }
        // 命令行覆盖
        for (Map.Entry<Object, Object> e : System.getProperties().entrySet()) {
            String k = String.valueOf(e.getKey());
            if (k.startsWith("netos.")) {
                values.put(k.substring("netos.".length()), String.valueOf(e.getValue()));
            }
        }
        return this;
    }

    /** 一次性覆盖，只给入口用：命令行 > 文件 > 默认。 */
    public Config set(String key, String value) {
        values.put(key, value);
        return this;
    }

    public String str(String key, String fallback) {
        String v = values.get(key);
        if (v != null && !v.isEmpty()) {
            return v;
        }
        return defaults.getOrDefault(key, fallback);
    }

    public int num(String key, int fallback) {
        try {
            return Integer.parseInt(str(key, String.valueOf(fallback)).trim());
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    public boolean bool(String key, boolean fallback) {
        String v = str(key, String.valueOf(fallback)).trim();
        return "true".equalsIgnoreCase(v) || "1".equals(v) || "yes".equalsIgnoreCase(v);
    }

    /** 只读视图，供 /api/kernel 观测。含生效值，不含密钥。 */
    public Map<String, String> view() {
        Map<String, String> out = new LinkedHashMap<>(defaults);
        out.putAll(values);
        return out;
    }
}