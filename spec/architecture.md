# 内核规格 · 一页纸

## 目标

单进程、零依赖、可观测、可缺腿运行。JDK 17，除 `com.sun.net.httpserver` 外不用任何外部库。

## 分层

```
netos.Main            启动参数、配置装载、装配、注册表、退出码
 └ netos.core.Kernel  侧面表 / 路由表 / 拍子 / 收摊
    ├ netos.core.Side 切口（唯一扩展点）
    ├ netos.http.*    请求 响应 路由函数 HTTP 服务器
    └ netos.sides.*   具体能力（本仓库只带 life 作样例）
```

## 契约

| 项 | 约定 |
|---|---|
| 侧面装配 | 按 `order` 升序；`init` 全做完再 `start` |
| 失败 | 任一方法抛异常 → 该面 `FAILED`，内核继续，退出码不变 |
| 收摊 | 按 `order` 降序 `stop`，单面异常不影响其余 |
| 路由 | `METHOD /path` 精确匹配，不做前缀通配 |
| 响应 | `{code,message,data,ts}`；`code=0` 为成功，HTTP 状态另算 |
| 来源 IP | 只用 socket 的 remoteAddress，请求头一律不采信 |
| 请求体 | 硬上限 1 MB，不跟配置走 |
| 配置 | 命令行 > 文件 > 默认；运行期不改形 |

## 为什么这样切

- **内核不认识业务**：它只认「面」和「路由」。加能力 = 加面，不改内核。
- **允许失败**：一个面挂掉不该拖死整机。运维要的是「还能用」，不是「全有或全无」。
- **观测优先**：`/`、`/api/kernel*` 只读面永不设闸；但只读面必须显式列举，避免写接口混进前缀。
- **传输层不认安全**：HTTP 层只调用注入进来的 `Guard`，它不知道什么叫令牌。

## 待办

- [x] `netos.http.HttpService`（已实现：1MB 上限、真实 IP 覆盖、Guard 注入）
- [x] `netos.sides.LifeSide` 样例面（含写面 `POST /api/life/poke`）
- [x] `security` 面：open / readonly / sealed 三档（`guarded` 为兼容别名）+ 双头令牌 + 自指豁免
      （17 项实测通过，含 fail-closed 与别名回归）
- [~] `runtime` 面：本仓库已实现可观测版（服务表 / JVM 堆·线程·GC / 60 拍历史）；
      上游 `ProcessTable` 与服务总线**不在本仓库**（旧 `runtime/` 快照已移出，
      见 README；需服务器源码或可编译桩）
- [x] build.sh + 自检（19 项内核 / 17 项安全 / 22 项契约冒烟 / 24 项失败路径）

## 安全面的三条硬规矩（写在这儿，防止以后被"优化"掉）

1. 观测面白名单逐条列举，禁止前缀匹配。
2. 本机豁免只认 socket 地址；`-`（拿不到）不算本机。
3. 令牌为空 = 全拒，不是"无密码"。