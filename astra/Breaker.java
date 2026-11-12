package astra;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * 熔断。模块连续失败就暂时不给它投消息，别让一个坏模块每拍拖累整条总线。
 *
 * 三个参数写死：连续 3 次失败跳闸，跳闸 20 拍后自动半开，
 * 半开时第一条消息成功就复位、再失败就接着跳。
 */
public final class Breaker {

    private static final int THRESHOLD = 3;
    private static final long OPEN_TICKS = 20;

    private int failures;
    private long openUntil;
    private long trips;

    public synchronized boolean open(long tick) {
        return tick < openUntil;
    }

    /** 半开：跳闸时间已过，先放一条过去试。 */
    public synchronized boolean halfOpen(long tick) {
        return openUntil != 0 && tick >= openUntil;
    }

    public synchronized void ok() {
        failures = 0;
        openUntil = 0;
    }

    public synchronized void fail(long tick) {
        failures++;
        if (failures >= THRESHOLD) {
            trips++;
            failures = 0;
            openUntil = tick + OPEN_TICKS;
        }
    }

    public synchronized void reset() {
        failures = 0;
        openUntil = 0;
    }

    public synchronized Map<String, Object> view(long tick) {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("open", open(tick));
        m.put("failures", failures);
        m.put("trips", trips);
        m.put("openUntilTick", openUntil);
        return m;
    }
}