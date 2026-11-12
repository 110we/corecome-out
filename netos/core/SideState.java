package netos.core;

/**
 * 侧面状态。
 *
 * NEW      已注册，未装配
 * INIT     装配中
 * RUNNING  已起跑
 * STOPPED  正常收摊
 * FAILED   init/start 抛了异常，内核照跑，只是这一面不提供服务
 */
public enum SideState {
    NEW,
    INIT,
    RUNNING,
    STOPPED,
    FAILED
}
