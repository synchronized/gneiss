<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 生产元数据查询迁移

## 范围

2026-11-09，继续 [PLAN-108](../plans/PLAN-108-meta-serialization-api.md)。
类型句柄新增 name 和按名称/ID 的 find_property；属性查找复用冻结属性范围，不复制属性列表。
Editor、Scene、序列化及实例访问的 describe_type/describe_property 调用改用句柄。
继承声明类型、只读能力和跨注册表检查保留；from_object 仍只接受对象类型。
本批未改变存档格式、资产调度或历史算法。

## 验证

Windows Clang Debug 默认构建通过，日志 build/plan108-query-build.log。
35 项 Meta、序列化、Prefab、属性编辑及 IPC CPU 回归通过（60.14 秒），
日志 build/plan108-query-tests.log。新增测试覆盖名称与 ID 查找一致性、缺失属性、
空句柄、非对象类型及注册表所有者释放后仍可读取元数据。
clang-format 差异检查与 git diff --check 通过。
未执行 GPU、大场景、完整平台矩阵或远端 CI。

## 剩余范围

旧查询 API、type_info/property_info 和迁移中的测试、枚举示例仍需删除或调整。
属性查找当前复用范围遍历，冻结索引及 Builder/适配、策略和 context 拆分尚未完成。
本批不表示 PLAN-108 或 DEV-052 完成。
