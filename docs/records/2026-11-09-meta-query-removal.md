<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 旧元数据查询删除

## 范围

2026-11-09，完成 [PLAN-108](../plans/PLAN-108-meta-serialization-api.md) 第 2 阶段收尾。
删除 describe_type、describe_type_at、describe_property 和 type_info/property_info，
不保留别名或转发接口。测试和属性检查示例使用现有 type/property 句柄。
新增 type_at 保持 GUID 排序枚举，schema_version 查询保留注册版本信息；
find_property 的便利入口返回同一属性句柄，不复制字段数组。
注册表内部属性类型与能力查询直接读取记录，不再临时组装信息结构。

## 验证

Windows Clang Debug 默认构建通过，日志 build/plan108-query-removal-build.log。
40 项定向 CPU 回归通过（59.34 秒），日志 build/plan108-query-removal-tests.log。
范围包括 Meta、序列化、Prefab、IPC、World 边界、组件适配器、材质编辑器与属性检查示例。
原有查询失败、并发读取和注册表生命周期检查迁移到句柄；
未注册类型用例改用自定义对象，因为 type_of 支持未单独注册的基础值。
源码、测试、示例及当前 Reference 无旧查询和信息结构引用。
clang-format 差异检查、git diff --check 通过。
未执行 GPU、大场景、完整平台矩阵及远端 CI。

## 后续

注册 Builder、自定义适配、冻结索引及策略/context 拆分仍按阶段 4 起推进。
本次未改变持久化格式，不表示 PLAN-108 或 DEV-052 全部完成。
