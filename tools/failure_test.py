#!/usr/bin/env python3
"""failure_test：失败路径自检（与 server_smoke 互补）。

只测仓库版真的具备的行为。**不测不存在的能力**：
墓碑区、进程表、模块热加载、令牌过期/撤销在仓库版里没有实现，
它们不在本文件里假装通过 —— 见 README「未覆盖的失败场景」。

覆盖：
  1. 端口占用：第二个进程必须以 3 退出，不半启动
  2. 先到的进程不受影响，继续服务
  3. 配置档位打错字：必须 fail-closed 到 readonly，不能变 open
  4. sealed + 空令牌：远端一律 401，观测面仍免闸
  5. 未知路由/错误方法：404，不 500
  6. 强杀后无监听残留，端口可立即再绑
  7. 同端口顺序重启两次都能起来（无 TIME_WAIT 残留依赖）
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


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
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


def call(host, port, path, method="GET", token=None, body=None, timeout=5):
    url = f"http://{host}:{port}{path}"
    req = urllib.request.Request(url, data=body, method=method)
    if token is not None:
        req.add_header("X-NetOS-Token", token)
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


def start(port, extra=()):
    args = ["java", "-cp", "out", "netos.Main",
            f"--kernel.port={port}", "--kernel.tick.ms=100", *extra]
    return subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)


def wait_ready(port, timeout=12):
    end = time.time() + timeout
    while time.time() < end:
        st, _ = call("127.0.0.1", port, "/api/kernel")
        if st == 200:
            return True
        time.sleep(0.1)
    return False


def port_free(port):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        s.bind(("0.0.0.0", port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def stop(proc, sig=signal.SIGTERM):
    if proc.poll() is not None:
        return proc.returncode
    proc.send_signal(sig)
    try:
        return proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
        return "KILLED"


def case_port_conflict():
    print("\n[1] 端口占用")
    port = free_port()
    a = start(port)
    check("先到进程就绪", wait_ready(port), "未就绪")

    b = start(port)
    try:
        code = b.wait(timeout=10)
    except subprocess.TimeoutExpired:
        b.kill()
        code = "HANG"
    out = b.stdout.read() if b.stdout else ""
    check("第二个进程以 3 退出", code == 3, f"exit={code}")
    check("输出点名 HTTP 启动失败", "HTTP 启动失败" in out, out[-200:])
    st, _ = call("127.0.0.1", port, "/api/kernel")
    check("先到进程仍在服务", st == 200, f"status={st}")

    stop(a)
    check("SIGTERM 后端口立即可绑", port_free(port))


def case_fail_closed_mode():
    print("\n[2] 档位拼写错误 fail-closed")
    port = free_port()
    p = start(port, ["--security.mode=guraded"])   # 故意拼错
    try:
        ok = wait_ready(port)
        check("拼错档位仍能启动（不静默 open）", ok)
        if ok:
            st, body = call("127.0.0.1", port, "/api/security")
            mode = body.get("data", {}).get("mode")
            check("落到 readonly 而不是 open", st == 200 and mode == "readonly",
                  f"{st} mode={mode}")
    finally:
        stop(p)
        check("退出后端口立即释放", port_free(port))


def case_sealed_empty_token(ip):
    print("\n[3] sealed + 空令牌（远端）")
    if not ip:
        print("SKIP 找不到非回环地址")
        return
    port = free_port()
    p = start(port, ["--security.mode=sealed", "--security.token="])
    try:
        if not wait_ready(port):
            check("sealed 启动", False)
            return
        st, _ = call(ip, port, "/api/life")
        check("远端 GET 401", st == 401, f"{st}")
        st, _ = call(ip, port, "/api/life/poke", "POST", body=b"x")
        check("远端 POST 401", st == 401, f"{st}")
        st, _ = call(ip, port, "/api/life/poke", "POST", token="", body=b"x")
        check("显式空令牌仍 401", st == 401, f"{st}")
        st, _ = call(ip, port, "/api/life/poke", "POST", token="x", body=b"x")
        check("任意令牌不因空配置放行", st == 401, f"{st}")
        st, _ = call(ip, port, "/api/kernel")
        check("观测面仍免闸", st == 200, f"{st}")
    finally:
        stop(p)
        check("退出后端口立即释放", port_free(port))


def case_bad_requests():
    print("\n[4] 坏请求不 500")
    port = free_port()
    p = start(port)
    try:
        if not wait_ready(port):
            check("启动", False)
            return
        st, body = call("127.0.0.1", port, "/api/nope")
        check("未知路由 404/404", st == 404 and body.get("code") == 404, f"{st} {body}")
        st, body = call("127.0.0.1", port, "/api/kernel", "DELETE")
        check("错误方法 404/404", st == 404 and body.get("code") == 404, f"{st} {body}")
    finally:
        stop(p)
        check("退出后端口立即释放", port_free(port))


def case_kill_residue():
    print("\n[5] 强杀残留")
    port = free_port()
    p = start(port)
    try:
        check("启动", wait_ready(port))
        code = stop(p, signal.SIGKILL)
        check("SIGKILL 退出", code == -9, f"exit={code}")
        deadline = time.time() + 5
        while time.time() < deadline and not port_free(port):
            time.sleep(0.1)
        check("强杀后 5s 内端口可绑（无监听残留）", port_free(port))
    finally:
        stop(p)


def case_restart_same_port():
    print("\n[6] 同端口顺序重启")
    port = free_port()
    for i in (1, 2):
        p = start(port)
        ok = wait_ready(port)
        check(f"第 {i} 次启动就绪", ok)
        stop(p)
        check(f"第 {i} 次退出后端口可绑", port_free(port))


def main():
    build = subprocess.run(["bash", "build.sh"], cwd=ROOT, capture_output=True, text=True)
    if build.returncode != 0:
        print(build.stdout + build.stderr)
        return 2
    print(build.stdout.strip())

    case_port_conflict()
    case_fail_closed_mode()
    case_sealed_empty_token(lan_ip())
    case_bad_requests()
    case_kill_residue()
    case_restart_same_port()

    total = passed + failed
    print(f"\n结果：{passed}/{total} 通过，{failed} 失败")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())