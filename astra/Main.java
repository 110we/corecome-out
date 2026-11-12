package astra;

/** Astra 内核的可执行入口。装配、签发、起边界、起心跳。 */
public final class Main {

    public static void main(String[] args) throws Exception {
        int port = 18400;
        for (String a : args) {
            if (a.startsWith("--astra.port=")) {
                port = Integer.parseInt(a.substring("--astra.port=".length()));
            }
        }

        Astra astra = new Astra();
        astra.install(new DemoModule());

        // 两张能力：一张只能往 demo.* 发，一张能撤销别人。
        astra.issue("demo", "demo.*", Cap.Right.SEND);
        astra.issue("admin", "*", Cap.Right.ADMIN, Cap.Right.SEND);
        astra.boot();

        Edge edge = new Edge(astra, port);
        edge.start();

        Thread ticker = new Thread(() -> {
            while (true) {
                try {
                    Thread.sleep(50);
                } catch (InterruptedException e) {
                    return;
                }
                astra.tick();
            }
        }, "astra-tick");
        ticker.setDaemon(true);
        ticker.start();

        Runtime.getRuntime().addShutdownHook(new Thread(edge::stop));
        System.out.printf("[astra] port=%d caps=demo,admin tick=50ms%n", port);
    }
}