#!/usr/bin/env python3
"""Astra 内核自检：能力签发、topic 覆盖、死信、撤销。

跑法：python3 tools/astra_selftest.py [port]
"""
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = int(sys.argv[1] if len(sys.argv) > 1 else 18400)

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


def call(path, method="GET", cap=None, body=b"", timeout=5, priority=None):
    url = f"http://127.0.0.1:{PORT}{path}"
    req = urllib.request.Request(url, data=body if method == "POST" else None, method=method)
    if cap is not None:
        req.add_header("X-Astra-Cap", cap)
    if priority is not None:
        req.add_header("X-Astra-Priority", str(priority))
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw = r.read().decode() or "{}"
            return r.status, json.loads(raw)
    except urllib.error.HTTPError as e:
        raw = e.read().decode() or "{}"
        try:
            return e.code, json.loads(raw)
        except json.JSONDecodeError:
            return e.code, {}
    except Exception as e:
        return f"ERR {e}", {}


def busy():
    s = socket.socket()
    try:
        return s.connect_ex(("127.0.0.1", PORT)) == 0
    finally:
        s.close()


def main():
    build = subprocess.run(["bash", "build.sh"], cwd=ROOT, capture_output=True, text=True)
    if build.returncode != 0:
        print(build.stdout + build.stderr)
        return 2

    if busy():
        print(f"端口 {PORT} 被占用")
        return 2

    log = open("/tmp/astra.log", "w+")
    proc = subprocess.Popen(["java", "-cp", "out", "astra.Main", f"--astra.port={PORT}"],
                            cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            if busy():
                break
            if proc.poll() is not None:
                log.seek(0)
                print(log.read())
                return 2
            time.sleep(0.1)

        st, b = call("/health")
        check("health 200", st == 200, st)
        check("内核自报 astra", b.get("kernel") == "astra", b.get("kernel"))
        check("demo 模块已装", "demo" in b.get("modules", []), b.get("modules"))

        st, b = call("/cmd/demo.hello/ping", "POST", cap="demo")
        check("持 demo 能力 → 202", st == 202, (st, b))

        st, _ = call("/cmd/demo.hello/ping", "POST")
        check("不带能力 → 403", st == 403, st)
        st, _ = call("/cmd/demo.hello/ping", "POST", cap="")
        check("空能力 → 403", st == 403, st)
        st, _ = call("/cmd/demo.hello/ping", "POST", cap="forged")
        check("伪造能力 → 403", st == 403, st)

        st, _ = call("/cmd/other.x/ping", "POST", cap="demo")
        check("能力不覆盖该 topic → 403", st == 403, st)

        st, _ = call("/cmd/sys.tick/beat", "POST", cap="admin")
        check("无订阅 topic 入队成功（落死信后置）", st == 202, st)

        time.sleep(0.4)
        st, b = call("/health")
        check("心跳在跑，消息已投递", b.get("dispatched", 0) >= 1, b.get("dispatched"))
        st, b = call("/dead")
        dead = json.dumps(b, ensure_ascii=False)
        check("死信队列有 NO_ROUTE", "NO_ROUTE" in dead, dead[:120])

        # —— 底子：入口校验（坏消息要在门口死，不进队列）
        st, b = call("/cmd/de*mo/ping", "POST", cap="demo")
        check("topic 含通配 → 400 BAD_TOPIC",
              st == 400 and b.get("error") == "BAD_TOPIC", (st, b))
        st, b = call("/cmd/demo.hello/9bad", "POST", cap="demo")
        check("command 不以字母开头 → 400 BAD_COMMAND",
              st == 400 and b.get("error") == "BAD_COMMAND", (st, b))
        st, b = call("/cmd/demo.hello/big", "POST", cap="demo", body=b"x" * 70000)
        check("payload 超 64K → 413 PAYLOAD_TOO_LARGE",
              st == 413 and b.get("error") == "PAYLOAD_TOO_LARGE", (st, b))
        st, b = call("/cmd/other.x/ping", "POST", cap="demo")
        check("拒收带原因：TOPIC_NOT_COVERED",
              b.get("error") == "TOPIC_NOT_COVERED", b)

        # —— 优先级与双队列
        st, b = call("/cmd/demo.hello/hi", "POST", cap="demo", priority="1")
        check("系统级消息被接收且回显优先级",
              st == 202 and b.get("priority") == 1, (st, b))
        time.sleep(0.3)
        st, b = call("/health")
        check("健康面有双队列", "queueSystem" in b and "queueUser" in b, list(b)[:8])

        # —— 熔断：连续失败就跳闸，不给它拖累总线
        for _ in range(4):
            call("/cmd/demo.hello/boom", "POST", cap="demo")
            time.sleep(0.15)
        st, b = call("/health")
        br = b.get("breakers", {}).get("demo", {})
        check("模块连续失败后跳闸", br.get("trips", 0) >= 1, br)
        st, b = call("/breaker/demo/reset", "POST", cap="admin")
        check("ADMIN 可复位熔断", st == 200, (st, b))

        # —— 审计与死信（存原文、可重放）
        st, b = call("/audit")
        check("审计有记录", st == 200 and len(b.get("audit", [])) > 0)
        st, b = call("/dead")
        items = b.get("dead", [])
        check("死信存了原文（含 command）",
              bool(items) and "command" in items[0], items[:1])
        if items:
            st, b = call(f"/replay/{items[0]['seq']}", "POST", cap="admin")
            check("ADMIN 可重放死信",
                  st in (200, 202) and b.get("replayed") is True, (st, b))

        st, _ = call("/revoke/demo", "POST", cap="demo")
        check("无 ADMIN 不能撤销 → 403", st == 403, st)
        st, b = call("/revoke/demo", "POST", cap="admin")
        check("admin 撤销 demo → 200", st == 200 and b.get("revoked") is True, (st, b))

        st, _ = call("/cmd/demo.hello/ping", "POST", cap="demo")
        check("撤销后旧能力立刻失效 → 403", st == 403, st)

        st, b = call("/health")
        check("能力表已减到 1", b.get("caps") == 1, b.get("caps"))
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()

    print(f"\n结果：{passed} 通过 / {failed} 失败")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())