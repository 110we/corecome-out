#!/usr/bin/env python3
"""security 面自检：真起进程、真从非回环地址打。

为什么必须从局域网 IP 打：
    本机豁免（127.0.0.1）永远放行，用它测三档等于什么都没测。
    只有走非回环地址，才会真正走进 open / guarded / sealed 的判定。
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
PORT = int(sys.argv[1] if len(sys.argv) > 1 else 18200)
TOKEN = "selftest-token-0421"

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


def lan_ip():
    """取本机非回环地址。沙箱里没有 iproute2，所以用 UDP「选路」再回退到 getaddrinfo。"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))  # 不发包，只为让内核选一个出口地址
        ip = s.getsockname()[0]
        if ip and not ip.startswith("127."):
            return ip
    except Exception:
        pass
    finally:
        s.close()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = info[4][0]
            if ip and not ip.startswith("127."):
                return ip
    except Exception:
        pass
    return None


def call(host, path, method="GET", token=None, timeout=5):
    url = f"http://{host}:{PORT}{path}"
    data = b"probe" if method == "POST" else None
    req = urllib.request.Request(url, data=data, method=method)
    if token is not None:
        req.add_header("X-NetOS-Token", token)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status
    except urllib.error.HTTPError as e:
        return e.code
    except Exception as e:
        return f"ERR {e}"


def busy(p=PORT):
    s = socket.socket()
    try:
        return s.connect_ex(("127.0.0.1", p)) == 0
    finally:
        s.close()


def run_case(mode, token, expects):
    """起一个进程跑一组断言，然后收掉。"""
    global PORT
    if busy():
        print(f"端口 {PORT} 被占用")
        return
    main_class = os.environ.get("KERNEL_MAIN", "netos.Main")
    args = ["java", "-cp", "out", main_class, f"--kernel.port={PORT}",
            f"--security.mode={mode}"]
    if token is not None:
        args.append(f"--security.token={token}")
    log = open(f"/tmp/netos-sec-{mode}.log", "w+")
    proc = subprocess.Popen(args, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    try:
        # 不能只看端口通：bind 成功到 handler 可用之间有一个窗口，
        # 这期间打进去会拿到连接错误，把「服务没起完」误判成「策略拒绝」。
        for _ in range(120):
            if call("127.0.0.1", "/api/kernel") == 200:
                break
            if proc.poll() is not None:
                log.seek(0)
                print(log.read())
                return
            time.sleep(0.1)
        for name, cond_fn in expects:
            check(f"[{mode}] {name}", cond_fn())
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()


def main():
    ip = lan_ip()
    if not ip:
        print("找不到非回环 IP，无法验证三档")
        return 2
    print(f"远程侧地址：{ip}  端口：{PORT}\n")

    build = subprocess.run(["bash", "build.sh"], cwd=ROOT, capture_output=True, text=True)
    if build.returncode != 0:
        print(build.stdout + build.stderr)
        return 2
    print(build.stdout.strip() + "\n")

    # open：远端读写都放行
    run_case("open", None, [
        ("远端 GET 放行", lambda: call(ip, "/api/life") == 200),
        ("远端 POST 放行", lambda: call(ip, "/api/life/poke", "POST") == 200),
    ])

    # readonly：远端读放行、写需令牌
    run_case("readonly", TOKEN, [
        ("远端 GET 放行", lambda: call(ip, "/api/life") == 200),
        ("远端 POST 无令牌 401", lambda: call(ip, "/api/life/poke", "POST") == 401),
        ("远端 POST 错令牌 401", lambda: call(ip, "/api/life/poke", "POST", token="wrong") == 401),
        ("远端 POST 正确令牌 200", lambda: call(ip, "/api/life/poke", "POST", token=TOKEN) == 200),
        ("观测面免闸", lambda: call(ip, "/api/kernel") == 200),
        ("本机 POST 豁免", lambda: call("127.0.0.1", "/api/life/poke", "POST") == 200),
    ])

    # 旧配置名 guarded 必须仍然可用（等价 readonly），否则升级即断服
    run_case("guarded", TOKEN, [
        ("别名 guarded 等价 readonly：远端 GET 放行", lambda: call(ip, "/api/life") == 200),
        ("别名 guarded：远端 POST 无令牌 401", lambda: call(ip, "/api/life/poke", "POST") == 401),
        ("别名 guarded：正确令牌 200", lambda: call(ip, "/api/life/poke", "POST", token=TOKEN) == 200),
    ])

    # sealed + 空令牌：不是"无密码"，是全拒
    run_case("sealed", "", [
        ("远端 GET 拒", lambda: call(ip, "/api/life") == 401),
        ("远端 POST 拒", lambda: call(ip, "/api/life/poke", "POST") == 401),
        ("空令牌不得放行", lambda: call(ip, "/api/life/poke", "POST", token="") == 401),
        ("任意令牌不得放行", lambda: call(ip, "/api/life/poke", "POST", token="x") == 401),
        ("观测面仍免闸", lambda: call(ip, "/api/kernel") == 200),
        ("本机仍豁免", lambda: call("127.0.0.1", "/api/life") == 200),
    ])

    print(f"\n结果：{passed} 通过 / {failed} 失败")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())