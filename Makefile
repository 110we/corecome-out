# 内核工作区入口。make test 跑全部三套自检。
JAVA ?= java

.PHONY: all build test test-netos test-security test-astra test-compat test-smoke test-failure test-compat bare bare-test clean

all: build

build:
	./build.sh

test-netos: build
	python3 tools/selftest.py 18130

test-security: build
	python3 tools/security_test.py 18210

test-astra: build
	python3 tools/astra_selftest.py 18410

test-compat: build
	python3 tools/selftest.py 18510
	KERNEL_MAIN=astra.compat.CompatMain python3 tools/selftest.py 18511
	KERNEL_MAIN=astra.compat.CompatMain python3 tools/security_test.py 18512

# 防漂移：面/路由/信封的契约快照
test-smoke: build
	python3 tools/server_smoke.py

# 失败路径：端口竞态、空令牌、强杀残留、拼错档位
test-failure: build
	python3 tools/failure_test.py

bare:
	$(MAKE) -C bare

bare-test:
	$(MAKE) -C bare test

test: test-netos test-security test-astra test-compat test-smoke test-failure

clean:
	rm -rf out