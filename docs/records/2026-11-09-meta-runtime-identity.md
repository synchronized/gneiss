<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 运行期类型身份

## 范围

2026-11-09，继续 [PLAN-108](../plans/PLAN-108-meta-serialization-api.md) 第 4 阶段。
注册表允许只有运行期 type_id 的 C++ 对象；多个空 GUID 类型不会相互覆盖。
类型查询、属性范围、嵌套访问和父对象定位使用运行期身份，属性句柄校验声明类型，
避免不同无 GUID 类型的同号属性被误用。空 GUID 本身不提供类型查询能力。
静态目标已知的普通 JSON 内容可编解码；多态信封与对象 value 快照缺少稳定 GUID 时
明确返回 unsupported，不把进程内 type_id 当作持久身份。
具体接口与边界见[反射参考](../reference/reflection.md)。

## 验证

Windows Clang Debug 默认构建通过，日志 build/plan108-runtime-id-build.log。
40 项相关 CPU 回归全部通过，日志 build/plan108-runtime-id-tests.log。
新增用例覆盖无 GUID 注册、重复注册、嵌套属性修改、跨类型属性误用、JSON 往返、
多态持久化及对象快照缺失 GUID 的拒绝行为。
既有回归覆盖继承、只读、序列化、场景、Prefab、IPC 与编辑器属性访问。
clang-format 差异检查及 git diff --check 通过。
未运行 GPU、大场景、远端 CI 或完整跨平台矩阵。

## 后续

第 4 阶段的内部访问边界清理尚未完成，后续策略和 context 拆分仍按计划推进。
本批不扩展无 GUID 对象的持久快照格式，也不增加历史格式兼容层。
