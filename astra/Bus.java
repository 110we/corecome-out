package astra;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.TreeMap;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 总线：校验、排队、路由、熔断、记账。没有业务，没有策略。
 *
 * 四条硬规矩：
 *   1. 入队先过 Envelope，坏消息不进队列。
 *   2. 系统队列永远先于用户队列——用户消息再多也饿不死内核自己。
 *   3. 模块连续失败就跳闸，跳闸期间不给它投，别人照常跑。
 *   4. 失败的消息连同原文进死信，可重放，但重放要重新过能力。
 */
public final class Bus {

    private final Map<String, List<Module>> routes = new ConcurrentHashMap<>();
    private final ConcurrentLinkedQueue<Message> system = new ConcurrentLinkedQueue<>();
    private final ConcurrentLinkedQueue<Message> user = new ConcurrentLinkedQueue<>();
    private final Map<String, Breaker> breakers = new ConcurrentHashMap<>();
    private final Map<String, AtomicLong> rejects = new ConcurrentHashMap<>();
    private final DeadLetters dead = new DeadLetters(128);
    private final Audit audit = new Audit(256);

    private final AtomicLong seq = new AtomicLong();
    private final AtomicLong accepted = new AtomicLong();
    private final AtomicLong dispatched = new AtomicLong();

    private final int maxInbox;
    private final int drainLimit;
    private final Cap internal;
    private volatile long tickSeq;

    public Bus(int maxInbox, int drainLimit, Cap internal) {
        this.maxInbox = maxInbox;
        this.drainLimit = drainLimit;
        this.internal = internal == null ? Cap.empty() : internal;
    }

    public void subscribe(String pattern, Module module) {
        routes.computeIfAbsent(pattern, k -> new ArrayList<>()).add(module);
    }

    public SendResult send(Message m) {
        SendResult env = Envelope.check(m);
        if (!env.ok()) {
            return reject(m, env);
        }
        if (m.cap().expired()) {
            return reject(m, SendResult.CAP_EXPIRED);
        }
        if (!m.cap().allows(Cap.Right.SEND)) {
            return reject(m, SendResult.NO_SEND);
        }
        if (!m.cap().covers(m.topic())) {
            return reject(m, SendResult.TOPIC_NOT_COVERED);
        }
        if (system.size() + user.size() >= maxInbox) {
            return reject(m, SendResult.QUEUE_FULL);
        }
        Message n = m.numbered(seq.incrementAndGet());
        (n.priority() <= Message.PRIORITY_SYSTEM ? system : user).add(n);
        accepted.incrementAndGet();
        audit.add(n, SendResult.OK);
        return SendResult.OK;
    }

    private SendResult reject(Message m, SendResult r) {
        rejects.computeIfAbsent(r.reason(), k -> new AtomicLong()).incrementAndGet();
        audit.add(m, r);
        return r;
    }

    /** 代投答复：模块不必持有能力，也不必认识 RPC。 */
    public void reply(Message req, Map<String, Object> data) {
        Map<String, Object> p = new LinkedHashMap<>(data);
        p.put("corr", String.valueOf(req.payload().getOrDefault("corr", "")));
        send(Message.of("kernel", "astra.reply", "result", p, internal,
                Message.PRIORITY_SYSTEM));
    }

    public int tick() {
        tickSeq++;
        int n = 0;
        Message m;
        while (n < drainLimit && (m = system.poll()) != null) {
            dispatch(m);
            n++;
        }
        while (n < drainLimit && (m = user.poll()) != null) {
            dispatch(m);
            n++;
        }
        return n;
    }

    private void dispatch(Message m) {
        List<Module> targets = new ArrayList<>();
        for (Map.Entry<String, List<Module>> e : routes.entrySet()) {
            String p = e.getKey();
            boolean hit = p.endsWith("*")
                    ? m.topic().startsWith(p.substring(0, p.length() - 1))
                    : p.equals(m.topic());
            if (hit) {
                targets.addAll(e.getValue());
            }
        }
        if (targets.isEmpty()) {
            bury(m, "NO_ROUTE");
            return;
        }
        for (Module module : targets) {
            Breaker b = breakers.computeIfAbsent(module.name(), k -> new Breaker());
            if (b.open(tickSeq)) {
                rejects.computeIfAbsent("MODULE_OPEN", k -> new AtomicLong()).incrementAndGet();
                continue;
            }
            try {
                module.onMessage(m, this);
                b.ok();
            } catch (RuntimeException ex) {
                b.fail(tickSeq);
                bury(m, "HANDLER_ERROR:" + module.name() + ":" + ex.getClass().getSimpleName());
            }
        }
        dispatched.incrementAndGet();
    }

    private void bury(Message m, String reason) {
        dead.add(m, reason);
        rejects.computeIfAbsent(reason.split(":")[0], k -> new AtomicLong()).incrementAndGet();
        audit.add(m.from(), m.topic(), m.command(), reason);
    }

    /** 重放一条死信：重新过 Envelope 与能力。过期就是过期，不给面子。 */
    public Optional<SendResult> replay(long deadSeq) {
        DeadLetters.Letter l = dead.get(deadSeq);
        if (l == null) {
            return Optional.empty();
        }
        return Optional.of(send(l.message()));
    }

    public Map<String, Object> health() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("inbox", system.size() + user.size());
        m.put("queueSystem", system.size());
        m.put("queueUser", user.size());
        m.put("accepted", accepted.get());
        m.put("dispatched", dispatched.get());
        m.put("deadLetters", dead.size());
        m.put("audit", audit.size());
        m.put("topics", new TreeMap<>(routes).keySet());
        m.put("modules", routes.values().stream().flatMap(List::stream)
                .map(Module::name).distinct().sorted().toList());
        Map<String, Object> bs = new TreeMap<>();
        breakers.forEach((k, v) -> bs.put(k, v.view(tickSeq)));
        m.put("breakers", bs);
        Map<String, Long> r = new TreeMap<>();
        rejects.forEach((k, v) -> r.put(k, v.get()));
        m.put("rejects", r);
        return m;
    }

    public List<Map<String, Object>> deadLetters() {
        return dead.view();
    }

    public List<Map<String, Object>> auditTail(int n) {
        return audit.tail(n);
    }

    public void resetBreaker(String name) {
        Breaker b = breakers.get(name);
        if (b != null) {
            b.reset();
        }
    }

    public Set<String> patterns() {
        return routes.keySet();
    }

    public long sequence() {
        return seq.get();
    }

    public long ticks() {
        return tickSeq;
    }
}