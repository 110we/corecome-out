#!/usr/bin/env bash
# 裸机 P0：启动 QEMU，轮询串口直到 ALL_OK 或超时，再逐项断言。
# 与 `make -C bare test` 的区别：不吞 QEMU 错误，不假定固定 10 秒足够。
set -uo pipefail
cd "$(dirname "$0")/../../bare" || exit 1

make || exit 1
rm -f build/serial.log build/qemu.log

qemu-system-aarch64 \
  -M virt,gic-version=2 -cpu cortex-a53 \
  -display none -monitor none -no-reboot \
  -serial file:build/serial.log \
  -D build/qemu.log -d guest_errors \
  -kernel build/kernel.elf 2>build/qemu.err &
qpid=$!

cleanup() {
  kill "$qpid" 2>/dev/null || true
  wait "$qpid" 2>/dev/null || true
}
trap cleanup EXIT

# 最多等 90 秒；正常开机在 1 秒内，慢 runner 也远小于此。
for _ in $(seq 1 90); do
  if grep -q 'ALL_OK' build/serial.log 2>/dev/null; then
    break
  fi
  if ! kill -0 "$qpid" 2>/dev/null; then
    break
  fi
  sleep 1
done

cleanup
trap - EXIT

bytes=$(wc -c < build/serial.log 2>/dev/null || echo 0)
echo "--- serial.log (${bytes} bytes) ---"
cat build/serial.log 2>/dev/null || true
echo "--- qemu.log (guest_errors) ---"
tail -n 80 build/qemu.log 2>/dev/null || true
echo "--- qemu.err ---"
tail -n 80 build/qemu.err 2>/dev/null || true
echo "--- end logs ---"

fail=0
require() {
  if grep -q -- "$1" build/serial.log 2>/dev/null; then
    echo "PASS $2"
  else
    echo "FAIL $2"
    fail=1
  fi
}

require '\[bare\] aarch64 virt' 'boot'
require 'vec: vbar=0x' 'vectors'
require 'gic: ready' 'gic'
require 'irq: ticks=.* ok' 'timer'
require 'mmu: sctlr=0x.* on' 'mmu'
require 'fault: caught=1 ok' 'fault'
require 'svc: caught=1 ok' 'svc'
require '\[sys\] heap ok' 'heap'
require '\[sys\] syscall ok' 'syscall'
require '\[kern\] mm: pages=128' 'mm'
require '\[kern\] slab ok' 'slab'
require '\[kern\] vfs: hello from ramfs' 'vfs'
require '\[kern\] rootfs:' 'rootfs'
require '\[thr\] A 0' 'thread A'
require '\[thr\] B 0' 'thread B'
require '\[kern\] sched: done' 'sched'
require 'BIG_KERNEL_OK' 'big kernel'
require 'ALL_OK' 'all'

if [ "$fail" -ne 0 ]; then
  echo "BARE_P0_FAIL"
  exit 1
fi

echo "BARE_P0_OK"
