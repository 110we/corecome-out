#!/usr/bin/env python3
"""server smoke：对「仓库版服务器内核」做契约快照。

这不是功能测试，是**防漂移**测试：
    面的名单、顺序、状态、路由集合、响应信封，任何一项变了都必须显式改这里。
    仓库版只有 2 面 / 13 路由；服务器版是 8 面 / 54 路由。
    两边补面时，同步更新 EXPECTED_*，否则这个脚本就是坏的。

用法：python3 tools/server_smoke.py [端口]
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
PORT = int(sys.argv[1] if len(sys.argv) > 1 else 18700)

# —— 契约快照（改面/改路由必须同步改这里）——
EXPECTED_SIDES = [("life", 60, "RUNNING"), ("runtime", 90, "RUNNING")]
EXPECTED_ROUTES = {
    "GET /",
    "GET /api/kernel",
    "GET /api/kernel/config",
    "GET /api/kernel/routes",
    "GET /api/life",
    "GET /api/life/heart",
    "GET /api/life/host",
    "GET /api/runtime",
    "GET /api/runtime/history",
    "GET /api/runtime/jvm",
    "GET /api/runtime/services",
    "GET /api/security",
    "POST /api/life/poke",
}

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


def free_port():
    s = socket.socket()
    try:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]
    finally:
        s.close()


def call(path, method="GET", body=b"", timeout=5, port=None):
    url = f"http://127.0.0.1:{port or PORT}{path}"
    req = urllib.request.Request(url, data=(body or None), method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read().decode())
        except Exception:
            return e.code, {}
    except Exception as e:
        return f"ERR {e}", {}


def envelope_ok(status, body):
    return (
        status == 200
        and set(body) == {"code", "message", "data", "ts"}
        and body["code"] == 0
        and isinstance(body["ts"], int)
        and body["ts"] > 0
    )


def main():
    global PORT
    PORT = free_port()
    print(f"冒烟端口：{PORT}\n")

    proc = subprocess.Popen(
        ["java", "-cp", "out", "netos.Main",
         f"--kernel.port={PORT}", "--kernel.tick.ms=100"],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    log = []
    try:
        ready = False
        for _ in range(100):
            if proc.poll() is not None:
                break
            st, _ = call("/api/kernel")
            if st == 200:
                ready = True
                break
            time.sleep(0.1)
        check("启动并就绪", ready, f"exited={proc.poll()}")
        if not ready:
            return finish(proc, log)

        st, body = call("/api/kernel")
        snap = body.get("data", {})
        check("信封 {code,message,data,ts}", envelope_ok(st, body), str(body)[:120])
        check("面数 = 2", snap.get("total") == 2, str(snap))
        check("存活面 = 2", snap.get("alive") == 2, str(snap))
        check("路由数 = 13", snap.get("routes") == 13, str(snap.get("routes")))
        sides = [(s["name"], s["order"], s["state"]) for s in snap.get("sides", [])]
        check("面名单/顺序/状态", sides == EXPECTED_SIDES, str(sides))
        check("拍子在走", snap.get("ticks", 0) >= 1, str(snap.get("ticks")))

        st, body = call("/api/kernel/routes")
        routes = set(body.get("data", {}).get("routes", []))
        check("路由集合逐条一致", routes == EXPECTED_ROUTES,
              f"多={sorted(routes - EXPECTED_ROUTES)} 少={sorted(EXPECTED_ROUTES - routes)}")

        st, body = call("/api/life/heart")
        beats1 = body.get("data", {}).get("beats", -1)
        time.sleep(0.35)
        st, body = call("/api/life/heart")
        beats2 = body.get("data", {}).get("beats", -1)
        check("life 心跳递增", beats1 >= 0 and beats2 > beats1, f"{beats1}→{beats2}")

        st, body = call("/api/runtime/services")
        svc = body.get("data", {})
        check("服务表覆盖全部路由", svc.get("count") == 13, str(svc.get("count")))
        check("服务全部 alive", all(s.get("alive") for s in svc.get("services", [])),
              str([s for s in svc.get("services", []) if not s.get("alive")])[:120])

        st, body = call("/api/runtime/history")
        hist = body.get("data", {})
        check("历史容量 60", hist.get("capacity") == 60, str(hist.get("capacity")))
        check("历史已有样本", hist.get("samples", 0) >= 1, str(hist.get("samples")))

        st, body = call("/api/security")
        sec = body.get("data", {})
        check("默认档 readonly", sec.get("mode") == "readonly", str(sec.get("mode")))
        check("默认无令牌", sec.get("tokenConfigured") is False, str(sec))
        check("观测面白名单 5 条", len(sec.get("observeWhitelist", [])) == 5, str(sec))

        st, body = call("/api/life/poke", "POST", b"probe")
        check("写路由可用（本机）", st == 200 and body.get("data", {}).get("ok") is True,
              f"{st} {str(body)[:80]}")

        st, body = call("/api/nope")
        check("未知路由 404", st == 404 and body.get("code") == 404, f"{st} {body}")
        st, body = call("/api/kernel", "POST")
        check("方法不匹配 404（不是 500）", st == 404, f"{st}")

        st, body = call("/api/life/poke", "POST", b"x" * (1024 * 1024 + 1))
        check("请求体超限 400", st == 400 and "超限" in body.get("message", ""),
              f"{st} {str(body)[:100]}")

        return finish(proc, log)
    finally:
        pass


def finish(proc, log):
    proc.terminate()
    try:
        code = proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
        code = "KILLED"
    # JVM 收到 SIGTERM 正常会以 143 退出（128+15），这不是失败；
    # 要验的是 hook 跑完、端口释放。0 表示进程自己收摊退出。
    check("收到 SIGTERM 后 10s 内退出", code in (0, 143), f"exit={code}")
    out = proc.stdout.read() if proc.stdout else ""
    if out:
        print("\n--- 进程输出（尾部）---")
        print("\n".join(out.strip().splitlines()[-12:]))

    # 端口必须立刻可再绑：不留监听残留
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        s.bind(("0.0.0.0", PORT))
        released = True
    except OSError as e:
        released = False
        print(f"bind 失败：{e}")
    finally:
        s.close()
    check("退出后端口立即可用", released)

    total = passed + failed
    print(f"\n结果：{passed}/{total} 通过，{failed} 失败")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())