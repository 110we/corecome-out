package netos.security;

import netos.http.Request;
import netos.http.Response;

/**
 * 闸门。
 *
 * 只有两种回答：null = 放行，Response = 拒绝（带着要回给调用方的话）。
 * 刻意不做 boolean —— 拒绝时必须能说明原因，否则排查只能靠猜。
 *
 * 传输层不认识它，它也不认识传输层：只吃 Request。
 */
public interface Guard {

    Response check(Request request);
}