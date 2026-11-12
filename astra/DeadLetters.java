package astra;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * 死信：失败的消息连同原文一起留下，理由写清楚。
 *
 * 上一版只留了"编号 + 理由"，等于把尸体烧了只记名字，没法验、也没法重放。
 * 满了丢最旧的一条。
 */
public final class DeadLetters {

    public record Letter(Message message, String reason, long at) {

        public Map<String, Object> view() {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("seq", message.seq());
            m.put("topic", message.topic());
            m.put("command", message.command());
            m.put("from", message.from());
            m.put("reason", reason);
            m.put("at", at);
            return m;
        }
    }

    private final int capacity;
    private final Map<Long, Letter> letters = new LinkedHashMap<>();

    public DeadLetters(int capacity) {
        this.capacity = capacity;
    }

    public synchronized void add(Message m, String reason) {
        letters.put(m.seq(), new Letter(m, reason, System.currentTimeMillis()));
        while (letters.size() > capacity) {
            Long oldest = letters.keySet().iterator().next();
            letters.remove(oldest);
        }
    }

    public synchronized Letter get(long seq) {
        return letters.get(seq);
    }

    public synchronized List<Letter> all() {
        return new ArrayList<>(letters.values());
    }

    public synchronized List<Map<String, Object>> view() {
        List<Map<String, Object>> out = new ArrayList<>();
        letters.values().forEach(l -> out.add(l.view()));
        return out;
    }

    public synchronized int size() {
        return letters.size();
    }
}