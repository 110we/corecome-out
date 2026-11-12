package astra;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * 审计：最近 N 条"谁、往哪、结果如何"。
 *
 * 只记判断结果，不记 payload —— 审计日志不该成为第二份数据副本。
 * 满了就丢最旧的：它是观察用的，不是账本。
 */
public final class Audit {

    public record Entry(long seq, String from, String topic, String command,
                        String result, long at) {

        public Map<String, Object> view() {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("seq", seq);
            m.put("from", from);
            m.put("topic", topic);
            m.put("command", command);
            m.put("result", result);
            m.put("at", at);
            return m;
        }
    }

    private final int capacity;
    private final Deque<Entry> ring = new ArrayDeque<>();

    public Audit(int capacity) {
        this.capacity = capacity;
    }

    public synchronized void add(Message m, SendResult result) {
        ring.addLast(new Entry(m.seq(), m.from(), m.topic(), m.command(),
                result.reason(), System.currentTimeMillis()));
        while (ring.size() > capacity) {
            ring.removeFirst();
        }
    }

    public synchronized void add(String from, String topic, String command, String result) {
        ring.addLast(new Entry(0, from, topic, command, result, System.currentTimeMillis()));
        while (ring.size() > capacity) {
            ring.removeFirst();
        }
    }

    public synchronized List<Map<String, Object>> tail(int n) {
        List<Entry> all = new ArrayList<>(ring);
        int from = Math.max(0, all.size() - Math.max(0, n));
        List<Map<String, Object>> out = new ArrayList<>();
        for (int i = from; i < all.size(); i++) {
            out.add(all.get(i).view());
        }
        return out;
    }

    public synchronized int size() {
        return ring.size();
    }
}