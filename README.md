# kernel · 代码空间

**主线只有一条：`bare/` —— AArch64 宏内核（EL0/EL1）。**
`netos/` 与 `astra/` 是**用户态服务层实验**（Side 契约、能力总线、HTTP 闸门），
它们能编译、能自检，但**不参与内核能力宣称**，也不要拿它们跟 Linux 比。

内核目标写死：**能跑自己编译的静态 EL0 程序**的教学级宏内核，
按 M0–M5 推进（见 [`bare/KERNEL.md`](bare/KERNEL.md#路线图m0m5)）。
**不追求 Linux ABI 兼容**：不跑 glibc、不跑现成 Linux 二进制、不承诺 POSIX 全量。
“语义对齐”指机制角色对得上，不是“能替换 Linux”。

## 环境（代码空间）

需要 JDK 17、**Python ≥ 3.9**（CI 用 3.12）、make；AArch64 裸机另需
`aarch64-linux-gnu-gcc`、`qemu-system-aarch64`，以及可选的 `e2fsprogs`
（有它才会生成 ext2 镜像并跑文件系统断言）。

- 构建：`./build.sh`
- 自检：`make test`
- 裸机：`make bare-test`
- 大内核（AArch64 裸机，EL0/EL1、4K 页表、COW fork、clone 线程、kill/信号、
  virtio-blk + 只读 ext2）：`cd bare && make && make test`，
  与 Linux 的能力对照见 [`bare/KERNEL.md`](bare/KERNEL.md)

## 现状

| 位置 | 内容 |
|---|---|
| `bare/` | **内核主线**：AArch64 EL0/EL1、4K 页表、COW fork、clone 线程、信号、ramfs + ext2、virtio-blk；`make test-m4` 跑通 EL0 init + shell（ls/cat）。M0–M4 已通，M5（DTB/驱动模型）未做 |
| `netos/` | **用户态服务层实验**（不是内核）：`core`（Kernel / Side / SideState / Config）、`security`（Guard / Policy 三档）、`http`、`sides`、`Main` |
| `astra/` | **用户态服务层实验**（不是内核）：能力总线（`Cap` / `Message` / `Module` / `Bus` / `Astra` / `Edge`），鉴权在总线上而不是 HTTP 层 |
| `tools/` | 五套自检 + 两个专项：`selftest.py`（NetOS 19 项）、`security_test.py`（三档，真从非回环地址打）、`astra_selftest.py`（能力总线 15 项）、`server_smoke.py`（面/路由/信封契约快照）、`failure_test.py`（失败路径 24 项） |
| `spec/` | 一页纸规格 |
| `Makefile` | `make build / test / clean` |
| 服务器原码 | `/home/yunhua/netos/src`（29–37 文件，4600–8600 行） |

> 原 `runtime/`（第七面上游快照）已移出仓库：它依赖服务器全量源码、不可编译，
> CI 也覆盖不到，留在主仓库只会制造“看起来能编”的错觉。需要时按 git 历史
> 找回，或把服务器版 `ProcessTable` 做成可编译桩后重新引入。

## 构建与自检

```
./build.sh                      # javac 17 → out/（netos + astra 一起编）
make test                       # 全部自检

# 单跑
python3 tools/selftest.py       # 起进程、打路由、验拍子、验收摊
python3 tools/security_test.py  # open/readonly/sealed：真从非回环地址打
python3 tools/astra_selftest.py # 能力签发、topic 覆盖、死信、撤销
python3 tools/server_smoke.py   # 面/顺序/路由集合/信封：契约快照
python3 tools/failure_test.py   # 端口占用、空令牌、强杀残留、拼错档位

# 无缝兼容验收：同一套断言原样打 Astra 内核
KERNEL_MAIN=astra.compat.CompatMain python3 tools/selftest.py
KERNEL_MAIN=astra.compat.CompatMain python3 tools/security_test.py
```

最近一次结果（本地，`make test`）：内核 `19/19`、安全 `17/17`
（含 `guarded` 别名回归）、Astra `26/26`、兼容 `19+17`、
契约冒烟 `22/22`、失败路径 `24/24`。CI 见
`.github/workflows/kernel.yml`。

## 仓库内核与服务器内核的关系

同源，不同步，各司其职：

- **仓库这份**：从零重写的可编译最小核，以 `Side` / `Kernel` 契约为准，用来验证接口与纪律。
  当前是 **2 面 / 13 路由**（`life`、`runtime` 及其路由）。
- **服务器那份**：在生产里跑的完整体（八面、54 路由、模块热加载）。
- 目标：面补齐后，两边只差「面」，不差「内核」。

**两边靠 `tools/server_smoke.py` 防漂移。** 面的名字、order、状态、
路由集合、响应信封都写成常量快照；仓库版加/删一个路由，冒烟必须同步改，
否则 CI 直接红。服务器版补面时，把 54 条路由的集合贴进来即可复用同一套脚本 ——
现在它证明的是“仓库这 13 条没漂”，不是“服务器 54 条都对”。

## 架构（标注：哪些在仓库里，哪些只在服务器版）

```
Kernel 装配
 ├─ Side（name / order / init / start / tick / stop / snapshot）
 │   init 5 · net 10 · security 20 · device 30      ← 仅服务器版
 │   sched 40 · memory 50                           ← 仅服务器版
 │   life 60 · runtime 90                           ← 仓库版只有这两个
 ├─ 路由：服务器 54 条 / 仓库 13 条，统一 {code,message,data,ts}
 ├─ 事件总线 / 存储 / 模块表                        ← 仅服务器版
 └─ 模块系统 Module(info/onLoad/onUnload) + 独立 ClassLoader  ← 仅服务器版
```

## 三条不可动的约定

1. **内核不认识"进程"**。进程表属于运行时这一面，与调度平级。内核一旦认识进程，就得懂生命周期、调度、回收——那它就不是内核了。
2. **传输层不 import security**。`HttpService` 只持一个 `Guard` 函数式接口，谁当闸门由 `Main` 决定。
3. **进程表是唯一登记处**。重名拒绝；终结进程进墓碑区（默认 256）；只有 Runtime 的 tick 能 reap。

## 安全三档（名字改了，语义也写清楚）

| 档 | 本机 | 远端只读 | 远端写 |
|---|---|---|---|
| `open` | 放行 | 放行 | 放行 |
| `readonly` | 放行 | 放行 | 需有效令牌 |
| `sealed` | 放行 | 需有效令牌 | 需有效令牌 |

- 旧名 `guarded` **保留为别名**，等价 `readonly`；不认识的档位一律
  fail-closed 到 `readonly`（打错一个字母不会变成 `open`）。
- 为什么改名：旧名让人以为它是“半防护”，实际它只拦写。
  保险的名字应该说明边界，而不是暗示强度。
- 令牌双头：`Authorization: Bearer` 或 `X-NetOS-Token`。
- 豁免两处：`GET` + 五个观测路径（`/`、`/api/kernel`、
  `/api/kernel/routes`、`/api/kernel/config`、`/api/security`）；本机回环。
  观测豁免是**字面匹配**，禁止前缀通配 —— `"/api/"` 会把未来的写接口一起放进去。
- 拿不到来源地址（`"-"`）**不算本机**。这一条曾是真实漏洞。
- **没有**令牌过期、撤销、并发会话：令牌是静态配置，轮换 = 重启。
  需要这些能力应加真正的会话层，而不是在字符串比较上叠语义。

## 交付纪律

- 端口被占：第二个进程以 **3** 退出，不半启动、不占表、不装作活着。
- 本仓库版**不写 pid 文件**。多实例隔离靠端口；进程监督交给 systemd/容器。
  `HttpServer` 绑定走内核默认的 `SO_REUSEADDR`：它只复用 `TIME_WAIT`，
  不会让两个监听者同时绑同一端口，所以“端口占用”测试是可靠的。
  强杀（SIGKILL）后监听 socket 随进程回收，端口立即可再绑（`failure_test` 验证）。
- 加固必留自救出口；不可恢复不是安全特性。

## 设计与 AI 生成：谁定了什么

这一节是为了让读者能正确归功、正确怀疑。

**人类（项目负责人）定的：**

- 三条不可动约定：内核不认识进程；传输层不 import security；进程表是唯一登记处。
- `Side` 契约（`name/order/init/start/tick/stop/snapshot`）与两轮装配（先全 init、再全 start）。
- 安全边界：观测面必须显式白名单、拿不到 IP 不算本机、空令牌一律拒绝。
- 统一响应信封 `{code,message,data,ts}`，以及“业务码与 HTTP 码不混用”。

**AI 写的：**

- 上述契约在 `netos/` 里的实现（Kernel / Side / HttpService / Policy / 两个面 / Main），
  以及 `tools/` 下的全部自检脚本、`bare/` 下的 AArch64 内核与文档。
- 测试暴露问题后的修复：例如“拿不到来源地址就当本机”是历史漏洞，
  由安全自检抓出；`fork/wait` 返回值被 dispatch 覆盖，由裸机断言抓出。

**可信度评级（别把自检通过当成生产就绪）：**

| 层面 | 证据 | 结论 |
|---|---|---|
| 接口/契约 | 编译 + 22 项契约冒烟 + 17 项安全自检 | 可验证，改动会红 |
| 单进程行为 | 19 项内核自检、24 项失败路径 | 覆盖常见路径，不覆盖长稳 |
| 并发/多实例 | 仅端口竞态与顺序重启 | **未证**：无压测、无长时间运行 |
| 服务器 54 路由 | 本仓库不包含 | **未证**，需拿生产快照跑同一套冒烟 |

引用架构价值时请记在人身上；引用代码可靠性时请按上表打折。

## 已测失败场景

| 场景 | 断言 | 位置 |
|---|---|---|
| 端口被占用 | 第二个进程 exit=3，先到者仍 200 | `failure_test.py` |
| 档位拼错（`guraded`） | 落到 `readonly`，不是 `open` | `failure_test.py` |
| sealed + 空令牌 | 远端 GET/POST 均 401，观测面仍 200 | `failure_test.py` / `security_test.py` |
| 未知路由 / 错误方法 | 404 且 `code=404`，不 500 | `failure_test.py` / `server_smoke.py` |
| 请求体 > 1 MB | 400，消息含“超限” | `server_smoke.py` |
| SIGKILL 强杀 | 5 秒内端口可再绑，无监听残留 | `failure_test.py` |
| 同端口顺序重启 | 两次都能就绪 | `failure_test.py` |
| SIGTERM 收摊 | 10 秒内退出，hook 跑完 | `server_smoke.py` |
| 旧档位名 `guarded` | 等价 `readonly`（升级不断服） | `security_test.py` |

## 未覆盖的失败场景（不假装测过）

- **墓碑区满 256**、**进程表重名/pid 残留**、**模块热加载 ClassLoader 泄漏**：
  仓库版没有进程表、墓碑区、模块系统，这些能力只在服务器版。
  要测必须先把服务器版的 `ProcessTable` / `Module` 做成可编译桩或接进 harness，
  当前没有做 —— 这是第 1 项待办的剩余部分。
- **令牌过期/撤销/并发**：没实现，见安全一节。
- **长时间运行、内存/线程泄漏、GC 压力**：没有 soak test。
- **多实例并发写日志/端口**：只测了顺序重启与端口冲突。
- **磁盘满/日志轮转**：Log 是追加写，没有容量上限与轮转测试。

## Non-goals（明确不承诺）

- **不追求 Linux ABI**：不跑 glibc、不跑现成 Linux 二进制、不承诺 POSIX 全量；
  目标是“能跑自己编译的静态 EL0 程序”的教学级宏内核。
- 内核主线只认 `bare/`；`netos/`、`astra/` 是用户态服务层实验，不算内核能力。
- 不支持 x86/ARM32/Windows；只承诺 AArch64 + Linux + JDK 17。
- 不承诺跨版本 ABI；`Side`/路由随时可能因补面而变，以 `server_smoke.py` 的快照为准。
- **墓碑满时的策略未定义**（拒绝新进程 / 丢最旧 / 阻塞），仓库版未实现，不在本文件承诺。
- **模块热加载失败不回滚**：当前没有热加载，服务器版需自行定义回滚语义。
- **调度语义不承诺**：单线程 tick、拍内串行、慢面拖整拍；无优先级、无公平性、无实时性。
- 不做多租户隔离，不替代 systemd/容器编排；本仓库不包含服务器 54 路由实现。

## 许可证

**AGPL-3.0-only**（见 `LICENSE`，GNU Affero GPL v3；文件正文没有 “or later” 条款，
所以按 only 处理）。网络分发修改版需提供对应源码。

## 裸机（bare/）

aarch64 裸机内核，现在是 EL1 **大内核**：异常向量、GICv2、100 Hz 定时器、MMU，
加上页分配/slab、COW fork、clone 线程、kill/信号、ramfs + VFS、
virtio-blk 与只读 ext2、**18 号系统调用**，全部编进同一个镜像、同一张页表，
彼此是函数调用。执行：`make bare-test`；CI 固定 `ubuntu-24.04`
（QEMU 8.2 / GCC 13）避免新版 QEMU 无串口。已知技术债与“接受什么、
推迟什么”写在 [`bare/KERNEL.md`](bare/KERNEL.md) 的「技术债与推迟项」。

    tools/ci_result.sh
