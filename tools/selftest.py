#!/usr/bin/env python3
"""NetOS 内核自检：真起进程、真打路由、真收摊。

不 mock，不桩。验的是「这份源码能不能跑」，
所以每一项都走网络，而不是直接调函数。
"""
import json
import os
import signal
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = int(sys.argv[1] if len(sys.argv) > 1 else 18110)
BASE = f"http://127.0.0.1:{PORT}"

passed = 0
failed = 0


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        passed += 1
        print(f"PASS {name}")
    else:
        failed += 1
        print(f"FAIL {name} {detail}")


def http(path, timeout=5):
    req = urllib.request.Request(BASE + path)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw = r.read().decode("utf-8", "replace")
            return r.status, json.loads(raw) if raw else {}
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        try:
            return e.code, json.loads(raw) if raw else {}
        except json.JSONDecodeError:
            return e.code, {"raw": raw}


def port_busy(p):
    s = socket.socket()
    try:
        return s.connect_ex(("127.0.0.1", p)) == 0
    finally:
        s.close()


def main():
    if port_busy(PORT):
        print(f"端口 {PORT} 已被占用，换一个再跑")
        return 2

    build = subprocess.run(["bash", os.path.join(ROOT, "build.sh")],
                           capture_output=True, text=True)
    if build.returncode != 0:
        print("构建失败：")
        print(build.stdout + build.stderr)
        return 2
    print(build.stdout.strip())

    log = open("/tmp/netos-selftest.log", "w+")
    main_class = os.environ.get("KERNEL_MAIN", "netos.Main")
    proc = subprocess.Popen(
        ["java", "-cp", "out", main_class, f"--kernel.port={PORT}"],
        cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)

    try:
        for _ in range(60):
            if port_busy(PORT):
                break
            if proc.poll() is not None:
                print("进程提前退出，日志：")
                log.seek(0)
                print(log.read())
                return 2
            time.sleep(0.1)
        else:
            print("等待监听超时")
            return 2

        st, body = http("/")
        check("GET / 200", st == 200, st)
        check("GET / name=NetOS", body.get("data", {}).get("name") == "NetOS")

        st, body = http("/api/kernel")
        data = body.get("data", {})
        check("GET /api/kernel 200", st == 200, st)
        check("内核存活面 >=1", data.get("alive", 0) >= 1, data.get("alive"))
        check("路由数 >=7", data.get("routes", 0) >= 7, data.get("routes"))

        st, body = http("/api/kernel/routes")
        check("GET /api/kernel/routes 200", st == 200, st)
        check("路由清单非空", body.get("data", {}).get("count", 0) >= 7)

        st, body = http("/api/kernel/config")
        check("GET /api/kernel/config 200", st == 200, st)
        check("配置含 tick.ms", "kernel.tick.ms" in body.get("data", {}))

        st, body = http("/api/life")
        check("GET /api/life 200", st == 200, st)
        check("life.side=life", body.get("data", {}).get("side") == "life")

        st, body = http("/api/runtime")
        check("GET /api/runtime 200", st == 200, st)
        st, body = http("/api/runtime/services")
        check("服务表已建立(>=10)", body.get("data", {}).get("count", 0) >= 10,
              body.get("data", {}).get("count"))
        st, body = http("/api/runtime/jvm")
        check("JVM 堆可读", body.get("data", {}).get("heapUsed", 0) > 0)
        _, h1 = http("/api/runtime/history")
        time.sleep(1.5)
        _, h2 = http("/api/runtime/history")
        check("历史在增长",
              h2.get("data", {}).get("samples", 0) > h1.get("data", {}).get("samples", 0))

        st, body = http("/nope")
        check("未知路由 404", st == 404, st)
        check("404 业务码非0", body.get("code") != 0, body.get("code"))

        _, b1 = http("/api/life/heart")
        time.sleep(2.2)
        _, b2 = http("/api/life/heart")
        beats1 = b1.get("data", {}).get("beats", -1)
        beats2 = b2.get("data", {}).get("beats", -1)
        check("拍子在跳", beats2 > beats1, f"{beats1} -> {beats2}")
    finally:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.seek(0)
        tail = log.read()
        log.close()

    check("进程已收摊", "已收摊" in tail, "日志里没有收摊记录")
    print(f"\n结果：{passed} 通过 / {failed} 失败")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())