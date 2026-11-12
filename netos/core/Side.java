package netos.core;

import java.util.Map;

/**
 * 侧面 —— 内核里一块可独立失败的能力。
 *
 * 设计要点：
 *   1. 每个方法都允许抛异常。核心里没有任何一处要求侧面必须成功；
 *      装配时捕获、标 FAILED、继续装下一个。「缺腿能站」是刻意的。
 *   2. 侧面不认识别的侧面。要通信只能走内核的服务总线，
 *      否则侧面之间会长成一张网，改一处塌一片。
 *   3. order 决定装配顺序，只保证「比我小的先装」，不保证别的。
 *      runtime 取 90（最后），因为它要提供「调用别人」的能力。
 */
public interface Side {

    /** 面名。唯一，重复即装配失败。 */
    String name();

    /** 装配序。越小越早。 */
    int order();

    /** 装配：注册路由与服务。此时还没有 tick。 */
    default void init(Kernel kernel) throws Exception {
    }

    /** 起跑：开监听、起线程。 */
    default void start() throws Exception {
    }

    /** 心跳：由内核主循环按拍调用。任何耗时操作都不该放这儿。 */
    default void tick() throws Exception {
    }

    /** 收摊：倒序调用，谁最后起谁先停。 */
    default void stop() throws Exception {
    }

    /** 观测快照：给自己的状态拍一张照，供 /api/kernel/sides 用。 */
    default Map<String, Object> snapshot() {
        return Map.of();
    }
}
