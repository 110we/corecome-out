package astra;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/** 一条消息。内核里唯一的货币。不可变，seq 由总线入队时补。 */
public record Message(
        long seq,
        String from,
        String topic,
        String command,
        Map<String, Object> payload,
        Cap cap,
        long at,
        int priority) {

    public static final int PRIORITY_SYSTEM = 0;
    public static final int PRIORITY_USER = 5;

    public Message {
        payload = payload == null ? Map.of()
                : Collections.unmodifiableMap(new LinkedHashMap<>(payload));
        topic = topic == null ? "" : topic;
        command = command == null ? "" : command;
        from = from == null ? "anon" : from;
        cap = cap == null ? Cap.empty() : cap;
        if (priority < 0 || priority > 9) {
            priority = PRIORITY_USER;
        }
    }

    public static Message of(String from, String topic, String command,
                             Map<String, Object> payload, Cap cap) {
        return of(from, topic, command, payload, cap, PRIORITY_USER);
    }

    public static Message of(String from, String topic, String command,
                             Map<String, Object> payload, Cap cap, int priority) {
        return new Message(0, from, topic, command, payload, cap,
                System.currentTimeMillis(), priority);
    }

    public Message numbered(long s) {
        return new Message(s, from, topic, command, payload, cap, at, priority);
    }

    public String key() {
        return topic + "/" + command;
    }
}