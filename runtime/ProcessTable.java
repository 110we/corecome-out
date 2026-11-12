package netos.runtime;

import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * 进程表。
 *
 * 内核里所有「正在跑的东西」都必须登记在这里，没有例外。
 * 这条规矩的价值在于：任何时候都能回答「现在有什么在动」，
 * 而且是唯一答案 —— 不用去翻各个模块自己的 Map。
 *
 * 三个刻意的约束：
 *   1. 名字不可重复（active 范围内）。重名会让观测失效，宁可拒绝创建。
 *   2. 终结的进程不立刻删除，保留在墓碑区（默认上限 256），便于事后追查。
 *   3. reap 只能由 Runtime 在 tick 里调用，业务代码不能主动清理别人的进程。
 */
public final class ProcessTable {

    /** 活跃进程：NEW / READY / RUNNING */
    private final Map<Long, Process> active = new ConcurrentHashMap<>();
    /** 名字 -> pid，用于重名检查 */
    private final Map<String, Long> nameIndex = new ConcurrentHashMap<>();
    /** 墓碑：已终结的进程，按进入顺序淘汰 */
    private final Map<Long, Process> graveyard = new LinkedHashMap<>();
    /** 外部 OS 进程托管表：osPid -> NetOS pid */
    private final Map<Long, Long> osIndex = new ConcurrentHashMap<>();

    private final int graveyardMax;

    public ProcessTable(int graveyardMax) {
        this.graveyardMax = Math.max(16, graveyardMax);
    }

    /* ==================== 创建 ==================== */

    /**
     * 创建一个进程。
     *
     * @throws IllegalStateException 名字已被活跃进程占用
     */
    public Process create(String name, String owner, Runnable body) {
        String n = name == null || name.isBlank() ? null : name.trim();
        if (n != null && nameIndex.containsKey(n)) {
            throw new IllegalStateException("进程名已被占用: " + n);
        }
        Process p = new Process(n, owner);
        p.body(body);
        active.put(p.pid(), p);
        if (n != null) {
            nameIndex.put(n, p.pid());
        }
        p.markReady();
        return p;
    }

    public Process create(String name, Runnable body) {
        return create(name, "kernel", body);
    }

    /* ==================== 查询 ==================== */

    public Process get(long pid) {
        Process p = active.get(pid);
        return p != null ? p : graveyard.get(pid);
    }

    public Process byName(String name) {
        if (name == null || name.isBlank()) {
            return null;
        }
        Long pid = nameIndex.get(name.trim());
        return pid == null ? null : active.get(pid);
    }

    public Process byOsPid(long osPid) {
        Long pid = osIndex.get(osPid);
        return pid == null ? null : active.get(pid);
    }

    public Collection<Process> activeAll() {
        return new ArrayList<>(active.values());
    }

    public Collection<Process> graveyardAll() {
        return new ArrayList<>(graveyard.values());
    }

    public int activeCount() {
        return active.size();
    }

    public int totalCreated() {
        return active.size() + graveyard.size();
    }

    /* ==================== 状态迁移 ==================== */

    /** 标记为运行中（由 Runtime 派发时调用） */
    public void running(Process p) {
        if (p != null && active.containsKey(p.pid())) {
            p.markRunning();
        }
    }

    /**
     * 终结一个进程并移入墓碑。
     *
     * @param failed 是否因失败而终结
     */
    public void terminate(Process p, boolean failed, String why) {
        if (p == null || p.isTerminal()) {
            return;
        }
        if (failed) {
            p.markFailed(why);
        } else {
            p.markStopped(why);
        }
        active.remove(p.pid());
        nameIndex.remove(p.name(), p.pid());
        if (p.osPid() > 0) {
            osIndex.remove(p.osPid());
        }
        synchronized (graveyard) {
            graveyard.put(p.pid(), p);
            while (graveyard.size() > graveyardMax) {
                Long oldest = graveyard.keySet().iterator().next();
                graveyard.remove(oldest);
            }
        }
    }

    /** 绑定一个真实 OS 进程的 pid，便于交叉核对 */
    public void bindOsPid(Process p, long osPid) {
        if (p == null) {
            return;
        }
        p.osPid(osPid);
        if (osPid > 0) {
            osIndex.put(osPid, p.pid());
        }
    }

    /* ==================== 收割 ==================== */

    /**
     * 收割已终结但还挂在 active 里的（理论上不该有，防御性保留）。
     * 由 Runtime 每个 tick 调用，返回被收割的数量。
     */
    public int reap() {
        int n = 0;
        for (Process p : activeAll()) {
            if (p.isTerminal()) {
                terminate(p, p.state() == Process.State.FAILED, p.reason());
                n++;
            }
        }
        return n;
    }

    /* ==================== 观测 ==================== */

    public Map<String, Object> summary() {
        Map<String, Object> m = new LinkedHashMap<>();
        int running = 0;
        int ready = 0;
        int failedEver = 0;
        for (Process p : active.values()) {
            if (p.state() == Process.State.RUNNING) {
                running++;
            } else if (p.state() == Process.State.READY) {
                ready++;
            }
        }
        for (Process p : graveyard.values()) {
            if (p.state() == Process.State.FAILED) {
                failedEver++;
            }
        }
        m.put("active", active.size());
        m.put("running", running);
        m.put("ready", ready);
        m.put("graveyard", graveyard.size());
        m.put("failedEver", failedEver);
        m.put("totalCreated", totalCreated());
        return m;
    }

    public List<Map<String, Object>> listActive() {
        List<Map<String, Object>> out = new ArrayList<>();
        for (Process p : active.values()) {
            out.add(p.view());
        }
        out.sort((a, b) -> Long.compare((long) a.get("pid"), (long) b.get("pid")));
        return out;
    }

    public List<Map<String, Object>> listGraveyard(int limit) {
        List<Map<String, Object>> out = new ArrayList<>();
        List<Map<String, Object>> all = new ArrayList<>();
        synchronized (graveyard) {
            for (Process p : graveyard.values()) {
                all.add(p.view());
            }
        }
        all.sort((a, b) -> Long.compare((long) b.get("pid"), (long) a.get("pid")));
        int n = Math.min(limit <= 0 ? 50 : limit, all.size());
        for (int i = 0; i < n; i++) {
            out.add(all.get(i));
        }
        return out;
    }

    /** 停掉全部活跃进程（内核收束时用） */
    public int terminateAll(boolean failed, String why) {
        int n = 0;
        for (Process p : activeAll()) {
            if (p.isActive()) {
                terminate(p, failed, why);
                n++;
            }
        }
        return n;
    }
}