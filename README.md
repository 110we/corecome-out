# kernel · 代码空间

两套内核：`netos/`（在用的那套，接口与纪律可编译验证）与 `astra/`（全新的，能力总线）。
能编译、能读、能改、能验。

## 环境（代码空间）

需要 JDK 17、python3、make；AArch64 裸机另需 `aarch64-linux-gnu-gcc` 与 `qemu-system-aarch64`。

- 构建：`./build.sh`
- 自检：`make test`
- 裸机：`make bare-test`

## 现状

| 位置 | 内容 |
|---|---|
| `netos/` | **在用的那套**：`core`（Kernel / Side / SideState / Config）、`security`（Guard / Policy 三档）、`util`（Log）、`http`（Request / Response / Handler / HttpService）、`sides`（LifeSide / RuntimeSide）、`Main` |
| `astra/` | **全新内核**：能力总线（`Cap` / `Message` / `Module` / `Bus` / `Astra` / `Edge` / `DemoModule` / `Main`），鉴权在总线上而不是 HTTP 层 |
| `tools/` | 三套自检：`selftest.py`（NetOS 19 项）、`security_test.py`（三档 14 项，真从非回环地址打）、`astra_selftest.py`（能力总线 15 项） |
| `runtime/` | 第七面（进程表 / 服务总线）上游真实源码快照，需服务器全量源码才可编译 |
| `spec/` | 一页纸规格 |
| `Makefile` | `make build / test / clean` |
| 服务器原码 | `/home/yunhua/netos/src`（29–37 文件，4600–8600 行） |

## 构建与自检

```
./build.sh                      # javac 17 → out/（netos + astra 一起编）
make test                       # 三套自检全跑

# 单跑
python3 tools/selftest.py       # 起进程、打路由、验拍子、验收摊
python3 tools/security_test.py  # open/guarded/sealed：真从非回环地址打
python3 tools/astra_selftest.py # 能力签发、topic 覆盖、死信、撤销

# 无缝兼容验收：同一套断言原样打 Astra 内核
KERNEL_MAIN=astra.compat.CompatMain python3 tools/selftest.py
KERNEL_MAIN=astra.compat.CompatMain python3 tools/security_test.py
```

最近一次结果：`编译完成：38 个 class` / 内核 `19 通过 0 失败` / 安全 `14 通过 0 失败` / Astra 底子 `26 通过 0 失败` / 兼容（同一套脚本打 Astra）`19 + 14 全过`。

## 仓库内核与服务器内核的关系

同源，不同步，各司其职：

- **仓库这份**：从零重写的可编译最小核，以 `Side` / `Kernel` 契约为准，用来验证接口与纪律。
- **服务器那份**：在生产里跑的完整体（八面、54 路由、模块热加载）。
- 目标：面补齐后，两边只差「面」，不差「内核」。

## 架构

```
Kernel 装配
 ├─ Side（name / order / init / start / tick / stop / snapshot）
 │   init 5 · net 10 · security 20 · device 30
 │   sched 40 · memory 50 · life 60 · runtime 90
 ├─ 路由 54 条，统一响应 {code,message,data,ts}
 ├─ 事件总线 / 存储 / 模块表
 └─ 模块系统 Module(info/onLoad/onUnload) + 独立 ClassLoader
```

## 三条不可动的约定

1. **内核不认识"进程"**。进程表属于运行时这一面，与调度平级。内核一旦认识进程，就得懂生命周期、调度、回收——那它就不是内核了。
2. **传输层不 import security**。`HttpService` 只持一个 `Guard` 函数式接口，谁当闸门由 `Main` 决定。
3. **进程表是唯一登记处**。重名拒绝；终结进程进墓碑区（默认 256）；只有 Runtime 的 tick 能 reap。

## 安全三档

| 档 | 行为 |
|---|---|
| open | 全放 |
| guarded | 验令牌，无效也放，但记账 |
| sealed | 必须有效令牌 |

令牌双头：`Authorization: Bearer` 或 `X-NetOS-Token`。
豁免两处：`/` 与只读观测面；`POST /api/security/token`（自指豁免，锁门后要能取钥匙）。
观测豁免只认 **GET/HEAD + 显式白名单字面匹配**，禁止前缀通配。

## 交付纪律

- 端口被占即退出，不半启动占表。
- pid 按端口隔离（`netos-<port>.pid`），多实例不互踩。
- 加固必留自救出口；不可恢复不是安全特性。
## 裸机（bare/）

aarch64 裸机内核，现在是一个 EL1 **大内核**骨架：异常向量、GICv2、100 Hz 定时器、MMU，加上页分配/slab、内核线程调度、ramfs + VFS、11 号系统调用，全部编进同一个镜像、同一张页表，彼此是函数调用。执行只在仓库内做：`make bare-test`，结果落 `bare/.ci/local-verify.log`。CI 暂未配置。

    tools/ci_result.sh
