package netos.runtime;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 运行时进程实体。
 *
 * 这里的「进程」不是操作系统进程 —— 它是 NetOS 自己的调度单位。
 * 一个 NetOS 进程可以是一个持续任务、一个服务的主循环、一次设备扫描，
 * 也可以是外部托管的真实 OS 进程（那样 osPid 才有值）。
 *
 * 状态机只有五个态，且单向居多：NEW -> READY -> RUNNING -> (STOPPED|FAILED)
 * 不提供 restart —— 重启是 stop + create 两步，由上层决定，内核不猜意图。
 */
public final class Process {

    public enum State {
        NEW, READY, RUNNING, STOPPED, FAILED
    }

    private static final AtomicLong SEQ = new AtomicLong();

    private final long pid;
    private final String name;
    private final String owner;
    private final long createdAt;

    private volatile State state = State.NEW;
    private volatile long startedAt;
    private volatile long endedAt;
    private volatile String reason = "";
    private volatile long osPid = -1L;
    private volatile int priority = 100;
    private volatile Runnable body;

    private final AtomicLong runCount = new AtomicLong();
    private final AtomicLong failCount = new AtomicLong();

    Process(String name, String owner) {
        this.pid = SEQ.incrementAndGet();
        this.name = name == null || name.isBlank() ? "proc-" + pid : name;
        this.owner = owner == null ? "kernel" : owner;
        this.createdAt = System.currentTimeMillis();
    }

    public long pid() {
        return pid;
    }

    public String name() {
        return name;
    }

    public String owner() {
        return owner;
    }

    public State state() {
        return state;
    }

    public long createdAt() {
        return createdAt;
    }

    public long startedAt() {
        return startedAt;
    }

    public long endedAt() {
        return endedAt;
    }

    public String reason() {
        return reason;
    }

    public long osPid() {
        return osPid;
    }

    public int priority() {
        return priority;
    }

    public long runCount() {
        return runCount.get();
    }

    public long failCount() {
        return failCount.get();
    }

    public Runnable body() {
        return body;
    }

    public long ageMs() {
        long end = endedAt > 0 ? endedAt : System.currentTimeMillis();
        return end - createdAt;
    }

    void body(Runnable r) {
        this.body = r;
    }

    void priority(int p) {
        this.priority = p;
    }

    void osPid(long p) {
        this.osPid = p;
    }

    void markReady() {
        this.state = State.READY;
    }

    void markRunning() {
        this.state = State.RUNNING;
        this.startedAt = System.currentTimeMillis();
        this.reason = "";
        runCount.incrementAndGet();
    }

    void markStopped(String why) {
        this.state = State.STOPPED;
        this.endedAt = System.currentTimeMillis();
        this.reason = why == null ? "" : why;
    }

    void markFailed(String why) {
        this.state = State.FAILED;
        this.endedAt = System.currentTimeMillis();
        this.reason = why == null ? "" : why;
        failCount.incrementAndGet();
    }

    public boolean isTerminal() {
        return state == State.STOPPED || state == State.FAILED;
    }

    public boolean isActive() {
        return state == State.READY || state == State.RUNNING;
    }

    public Map<String, Object> view() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("pid", pid);
        m.put("name", name);
        m.put("owner", owner);
        m.put("state", state.name());
        m.put("priority", priority);
        m.put("osPid", osPid);
        m.put("createdAt", createdAt);
        m.put("startedAt", startedAt);
        m.put("endedAt", endedAt);
        m.put("ageMs", ageMs());
        m.put("runCount", runCount.get());
        m.put("failCount", failCount.get());
        m.put("reason", reason);
        return m;
    }
}