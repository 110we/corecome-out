# 构建与测试

## 目录约定

| 目录 | 作用 |
|---|---|
| `kernel/` | 裸机宏内核（AArch64），自带 `Makefile` |
| `services/` | Java 用户态服务层，源码树即包结构 |
| `tests/` | 集成 / 安全 / QEMU 测试 |
| `tools/build/` | 构建脚本 |

## Java 服务层

```bash
make build            # = bash tools/build/build.sh，javac 17 → out/
make test             # 五套自检 + 兼容验收
```

## 裸机内核

```bash
make kernel           # 只编译
make kernel-test      # QEMU 开机 + P0 断言（需要 qemu-system-aarch64）
make kernel-m4        # M4 验收：EL0 init → shell → ls/cat
```

内核构建需要 `aarch64-linux-gnu-gcc`、`qemu-system-aarch64`，ext2 断言额外需要 `e2fsprogs`。
在没有 `e2fsprogs` 的机器上，块设备与 ext2 两项会自动跳过。
