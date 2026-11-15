# 仓库入口。make test 跑全部自检；make kernel 编裸机内核。
JAVA ?= java

.PHONY: all build test test-netos test-security test-astra test-compat \
        test-smoke test-failure kernel kernel-test kernel-m4 clean

all: build

build:
	bash tools/build/build.sh

test-netos: build
	python3 tests/integration/selftest.py 18130

test-security: build
	python3 tests/security/security_test.py 18210

test-astra: build
	python3 tests/integration/astra_selftest.py 18410

test-compat: build
	python3 tests/integration/selftest.py 18510
	KERNEL_MAIN=services.astra.compat.CompatMain python3 tests/integration/selftest.py 18511
	KERNEL_MAIN=services.astra.compat.CompatMain python3 tests/security/security_test.py 18512

# 防漂移：面/路由/信封的契约快照
test-smoke: build
	python3 tests/integration/server_smoke.py

# 失败路径：端口竞态、空令牌、强杀残留、拼错档位
test-failure: build
	python3 tests/integration/failure_test.py

kernel:
	$(MAKE) -C kernel

kernel-test:
	$(MAKE) -C kernel test

kernel-m4:
	$(MAKE) -C kernel test-m4

test: test-netos test-security test-astra test-compat test-smoke test-failure

clean:
	rm -rf out
	$(MAKE) -C kernel clean
