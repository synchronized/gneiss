<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-279：Granit 0.39.0 升级与能力审计

## 当前结果

2026-09-29，Gneiss 提交 `4493b8f` 将默认 Granit 由 0.30.0 升级至正式 0.39.0，锁定
`c832c8328ef54bd13746ab14b2f99a9e0aca7531`。同步更新 Fetch 默认迁移、Package/安装消费者
最低版本和编译断言。未修改上游源码，仍使用原生 Window 后端。

Windows Clang Debug Shared 完整构建通过，158/158 CTest 通过（130.17 秒），包含安装消费者、
原有 GPU 像素、Runtime/Editor 工作流；默认版本迁移、显式固定与覆盖测试通过。
日志位于本地 `build/0.43-configure-debug.log`、`0.43-build-debug.log`、
`0.43-granit039-debug-tests.log` 和 `0.43-version-cache.log`。

## 升级图像对照

使用 0.42 固定 Sponza 日常资产、Debug 线程池模式，未更改材质的升级版本捕获
`build/0.43-upgrade-baseline/daily-thread.ppm`，SHA-256 为
`8440aa72493c0675389b4c20fac61c8539186b39f1065810cd9df77bb0e7f650`，与
[0.42 正式基线](artifacts/0.42-scene-matrix.json) 的日常配置完全一致。
节点 553、资源 458，单次激活 0.8084 ms。

这次捕获与后续构建准备存在并行 CPU 工作，只用于升级图像对照，不作为 0.43 性能改善证据。
完整配置、新材质压力与发布平台矩阵留待 M-284/M-285；本记录不代表 0.43 已完成或已发布。

## 能力缺口

标准 PBR 已有五类贴图参数，Gneiss 应自行补齐其资产和生命周期链路。
透明 Pass、MASK/双面、逐槽 UV/采样器及负缩放切线手性按
[UPSTREAM-043](../plans/UPSTREAM-043-granit-pbr.md) 跟踪，区分确认缺口、数学复现和待 GPU 验证项。
不得从基础图像不变推断未使用的 PBR 能力正确。
