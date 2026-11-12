package netos.http;

/** 路由处理函数。可能抛异常，由 HttpService 统一转 500。 */
@FunctionalInterface
public interface Handler {
    Response handle(Request request) throws Exception;
}