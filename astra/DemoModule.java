package astra;

import java.util.concurrent.atomic.AtomicLong;

/**
 * 演示模块：证明"安装即可被调用"。
 *
 * 它没有能力、不认得任何人，只数自己收到了几条。
 */
public final class DemoModule implements Module {

    private final AtomicLong seen = new AtomicLong();
    private volatile String last = "";

    @Override
    public String name() {
        return "demo";
    }

    @Override
    public String[] subscribes() {
        return new String[]{"demo.*"};
    }

    @Override
    public void onMessage(Message m, Bus bus) {
        seen.incrementAndGet();
        last = m.key();
        if ("boom".equals(m.command())) {
            // 自检用的炸药：证明熔断真的会跳闸，而不是只在文档里存在。
            throw new IllegalStateException("boom（自检用）");
        }
    }

    public long seen() {
        return seen.get();
    }

    public String last() {
        return last;
    }
}