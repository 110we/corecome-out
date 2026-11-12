package astra;

/**
 * 模块：用户态的一切都由它实现（驱动、协议、业务）。
 *
 * 模块之间不互相引用——这是内核保持小的前提。
 * 想调用别人就发消息；想被调用就订阅。
 */
public interface Module {

    String name();

    /** 订阅的 topic；结尾 * 表示前缀。空数组表示只发不收。 */
    String[] subscribes();

    void onMessage(Message m, Bus bus);
}