package astra;

/**
 * 投递结果。布尔值在这里不够用——"没成功"有五种，处理方式各不相同：
 * 能力过期要重签，队列满要退避，格式错是调用方的 bug。
 */
public enum SendResult {

    OK(202),
    BAD_TOPIC(400),
    BAD_COMMAND(400),
    PAYLOAD_INVALID(400),
    PAYLOAD_TOO_LARGE(413),
    CAP_EXPIRED(403),
    NO_SEND(403),
    TOPIC_NOT_COVERED(403),
    QUEUE_FULL(429);

    private final int http;

    SendResult(int http) {
        this.http = http;
    }

    public boolean ok() {
        return this == OK;
    }

    public int http() {
        return http;
    }

    /** 稳定的机器可读名。日志与断言都用它，不用中文。 */
    public String reason() {
        return name();
    }
}