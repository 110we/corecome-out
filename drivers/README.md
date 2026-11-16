# 驱动

设备驱动：总线、定时器、中断、块存储、网络、USB、显示、输入、GPU。

## 当前驱动模型

- FDT 解析器从设备树收集带 compatible 和 reg 的 MMIO 设备实例。
- driver_model.c 提供固定容量设备表与驱动表，按 compatible 调用 probe，成功后绑定。
- virtio_blk.c 是第一个接入模型的驱动；块设备地址来自 FDT，不再手工扫描固定地址。
- host 测试位于 kernel/tests/driver_registry_test.c，由 make -C kernel test-driver-host 执行。

当前是教学级第一版：无动态卸载、热插拔、总线层级、phandle 中断映射或通用 DMA/IOMMU 资源管理；PL011/GIC/定时器仍有直接初始化路径。
