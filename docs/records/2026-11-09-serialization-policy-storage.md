<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 序列化策略存储迁移

2026-11-09，继续 [PLAN-108](../plans/PLAN-108-meta-serialization-api.md) 第 5 阶段。
删除 type_definition 的专用 serialization 字段，注册表内部只保存通用扩展元数据。
find_serialization 从 Meta 迁到 Serialization；内置资产及测试资产使用 metadata 登记策略，
所有查询消费者显式包含序列化策略声明。没有增加第二套注册表或缓存。

## 验证

构建 engine、类型注册、实例、序列化、action_map、asset_manager 目标通过。
日志 build/plan108-policy-storage-build.log、build/plan108-policy-storage-verified-build.log。
对应 5 项测试通过，日志 build/plan108-policy-storage-tests.log 和
build/plan108-policy-storage-asset-tests.log；git diff --check 通过。
本批没有重跑其他宿主与编辑器目标、GPU、大场景或远端 CI。

## 剩余迁移

registration 的 serialization/validate 快捷方法仍暂存 binding 并在 commit 存入扩展；
这属于第 5 阶段过渡，尚未满足 Meta 完全不依赖 Serialization 的目标。
后续须拆分 content/reference/field 策略、删除 reflected 和旧 flags，再收敛上下文及布局。
