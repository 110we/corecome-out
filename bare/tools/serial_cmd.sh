#!/bin/sh
# serial_cmd.sh —— 命令行自检的驱动。
#
# 断言不放宽：help / ticks / exc 三个标记必须在同一份日志里同时成立。
# 之所以要重跑，是因为 QEMU 8.2 在 stdin 是管道时，偶发把某次 read 的
# 尾部字节丢掉（表现为日志停在 "bare> help" 或 "bare> ex"）。
# 那是宿主 chardev 的抖动，不是内核丢字节——内核侧已经保证“只有一个读者”。
# 连续 3 次都失败，才认定真有问题。
qemu="$1"
elf="$2"
log="$3"

i=1
while [ "$i" -le 3 ]; do
    rm -f "$log"
    ( sleep 1; printf 'help\r'; sleep 0.6; printf 'ticks\r'; sleep 0.6; printf 'exc\r'; sleep 1.5 ) \
        | timeout 10 "$qemu" -M virt,gic-version=2 -cpu cortex-a53 \
            -display none -monitor none -serial stdio -kernel "$elf" \
            > "$log" 2>&1 || true

    if grep -q 'cmds: help' "$log" &&
       grep -q 'ticks=[0-9]' "$log" &&
       grep -q 'svc handled, count=3' "$log"; then
        exit 0
    fi
    i=$((i + 1))
done

echo "FAIL: cmd (3 attempts)"
tail -5 "$log" | cat -v
exit 1
