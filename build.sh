#!/usr/bin/env bash
# 构建：javac 17，输出 out/
set -e
cd "$(dirname "$0")"

OUT=out
rm -rf "$OUT"
mkdir -p "$OUT"

find netos astra -name '*.java' > /tmp/kernel-src.txt
javac -encoding UTF-8 --release 17 -d "$OUT" @/tmp/kernel-src.txt

COUNT=$(find "$OUT" -name '*.class' | wc -l | tr -d ' ')
echo "编译完成：${COUNT} 个 class → ${OUT}/"
