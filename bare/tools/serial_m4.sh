#!/bin/sh
# serial_m4.sh —— M4 验收：EL0 init -> fork/exec -> shell -> ls / cat。
#
# 断言（同一份日志里同时成立）：
#   1. "init: up, starting shell"     内核把 init 以 EL0 跑起来
#   2. "sh> ls" 之后有 "hello.txt"      shell 用 SYS_LIST 读了 ramfs root
#   3. "big kernel vfs works"          cat 走 open/read/write 打到 /hello.txt
#   4. "init: shell exited" + code=0   init wait 到 shell 退出、收尸成功
#
# QEMU 8.2 管道 stdin 偶发丢尾字节，重试 3 次。
qemu="$1"
elf="$2"
log="$3"

i=1
while [ "$i" -le 3 ]; do
    rm -f "$log"
    ( sleep 1
      printf 'init\r'; sleep 1.0
      printf 'ls\r';   sleep 0.6
      printf 'cat\r';  sleep 0.6
      printf 'e\r';    sleep 1.5
    ) | timeout 12 "$qemu" -M virt,gic-version=2 -cpu cortex-a53 \
            -display none -monitor none -serial stdio -kernel "$elf" \
            > "$log" 2>&1 || true

    if grep -q 'init: up, starting shell' "$log" &&
       grep -q 'hello.txt' "$log" &&
       grep -q 'big kernel vfs works' "$log" &&
       grep -q 'init: shell exited' "$log" &&
       grep -q 'wait pid=[0-9]* code=0' "$log"; then
        exit 0
    fi
    i=$((i + 1))
done

echo "FAIL: m4 (3 attempts)"
tail -20 "$log" | cat -v
exit 1
