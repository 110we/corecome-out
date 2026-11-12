#!/usr/bin/env bash
# 读 CI 的结论。GitHub Actions 把结果回写到 kernel/bare/.ci/，
# 所以只需要 git fetch，不需要 token，也不需要开网页。
set -u
cd "$(dirname "$0")/../.." || exit 1

git fetch -q origin 2>/dev/null || { echo "fetch 失败：检查 deploy key / 网络"; exit 1; }

found=0
for ref in origin/main origin/ci/last-result; do
  if git cat-file -e "$ref:kernel/bare/.ci/last-result.json" 2>/dev/null; then
    echo "== $ref"
    git show "$ref:kernel/bare/.ci/last-result.json"
    found=1
    break
  fi
done

if [ "$found" != 1 ]; then
  if [ -f kernel/bare/.ci/local-result.json ]; then
    echo "== 仓库内执行（本地）"
    cat kernel/bare/.ci/local-result.json
    exit 0
  fi
  echo "还没有结果：CI 未跑，本地也还没执行过"
  exit 2
fi

for ref in origin/main origin/ci/last-result; do
  if git cat-file -e "$ref:kernel/bare/.ci/serial.log" 2>/dev/null; then
    echo "== 串口（$ref）"
    git show "$ref:kernel/bare/.ci/serial.log" | tail -20
    break
  fi
done
