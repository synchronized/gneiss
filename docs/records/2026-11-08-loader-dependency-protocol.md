<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Loader 必需依赖调度实施记录

日期：2026-11-08。对应 [PLAN-107](../plans/PLAN-107-asset-model-loading-protocol.md) 第一阶段。

## 实现

- asset_loader 声明依赖并通过 finish 装配；删除 resolve_dependencies(asset_manager&)。
- asset_manager 统一推进必要依赖，全部完成后装配，失败沿用资产诊断。
- Scene Loader 删除私有请求表和轮询，使用类型化 Prefab 内容结果槽。
- 依赖完成时固定内容快照；独立重载可以更新共享身份，但不会替换已取得的依赖内容。
- 声明结果槽不发起 I/O；释放未完成加载会释放对应请求观察者。

## 验证

Windows Clang Debug 默认构建成功。
定向 CPU 回归 35/35 通过，耗时 59.28 秒，覆盖资产管理、场景、Prefab 和相关边界。
新增检查覆盖声明无读取副作用、依赖缓存命中以及独立重载后依赖快照保持不变。
本地日志：build/plan107-protocol-build.log、build/plan107-protocol-tests.log。

未运行 GPU、大场景、跨平台与远端 CI。

## 剩余范围

本次仅迁移 Scene→Prefab 必需内容依赖。通用依赖图、reference 策略、环检测、其他 Loader、
统一加载 API 和 CPU/后端生命周期解耦仍按 PLAN-107 后续阶段实施，不属于本次完成范围。
