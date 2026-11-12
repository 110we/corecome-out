package netos.util;

import java.time.LocalTime;
import java.time.format.DateTimeFormatter;

/**
 * 日志。
 *
 * 一条日志 = 时间 + 级别 + 标签 + 消息。
 * 不用日志框架：单文件、零依赖、可被 grep。格式化只有一处，
 * 因为"日志格式变了"通常意味着有人开始用正则解析日志 —— 那就该改接口了。
 */
public final class Log {

    private static final DateTimeFormatter T = DateTimeFormatter.ofPattern("HH:mm:ss.SSS");
    private static volatile boolean verbose = true;

    private Log() {
    }

    public static void verbose(boolean on) {
        verbose = on;
    }

    public static void info(String tag, String fmt, Object... args) {
        write("INFO ", tag, fmt, args, null);
    }

    public static void warn(String tag, String fmt, Object... args) {
        write("WARN ", tag, fmt, args, null);
    }

    public static void error(String tag, String fmt, Object... args) {
        write("ERROR", tag, fmt, args, null);
    }

    public static void errorStack(String tag, String msg, Throwable t) {
        write("ERROR", tag, msg, new Object[0], t);
        if (t != null) {
            StackTraceElement[] trace = t.getStackTrace();
            int n = Math.min(trace.length, 6);
            for (int i = 0; i < n; i++) {
                System.err.println("    at " + trace[i]);
            }
        }
    }

    private static void write(String level, String tag, String fmt, Object[] args, Throwable t) {
        String msg = (args == null || args.length == 0) ? fmt : String.format(fmt, args);
        String line = LocalTime.now().format(T) + " " + level + " [" + tag + "] " + msg;
        if (verbose) {
            System.out.println(line);
        }
        if ("ERROR".equals(level) || "WARN ".equals(level)) {
            System.err.println(line);
        }
    }

    public static void close() {
        System.out.flush();
        System.err.flush();
    }
}