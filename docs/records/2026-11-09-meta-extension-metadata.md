<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 扩展元数据基础

2026-11-09，推进 [PLAN-108](../plans/PLAN-108-meta-serialization-api.md) 第 5 阶段。
现有注册表新增类型和属性的只读扩展元数据存储，按 C++ 类型查询；
注册器拥有副本，冻结句柄保活，不增加全局注册表。继承属性复制冻结记录时保留扩展。
本批只完成策略存储基础；原 binding 与 flags 尚未迁移，不能宣称分层已完成。

## 验证

构建 gneiss_type_builder_test 与 gneiss_meta_instance_test（含 engine）通过，
日志 build/plan108-metadata-build.log 和 build/plan108-metadata-verified-build.log。
2 项定向测试通过，日志 build/plan108-metadata-tests.log。
新增用例覆盖类型/字段扩展查询、重复登记拒绝、字段不存在、输入副本隔离及句柄保活。
格式差异检查和 git diff --check 通过。
未重跑全部调用方构建、完整 CPU 回归、GPU、大场景或远端 CI；后续策略迁移统一验证。
