<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-060：类型化组件描述列表

## 状态

已完成。决策见 [ADR-093](../decisions/ADR-093-component-descriptions.md)。

## 目标与非目标

对象统一保存 components 列表，以描述类型 GUID 和内容序列化 Camera、MeshRenderer。
保留对象变换、层级及 Prefab 来源；不实现继承反射、任意对象图或同类型多组件。
未发布格式直接迁移，更新仓库资产、生成器、测试及格式文档，不保留旧组件对象格式。

## 实施与验收

- [x] 不可变组件描述所有者、类型检查及 GUID 编解码。
- [x] 内置描述注册；场景和 Prefab 读写统一列表，未知类型、重复类型和非法内容报错。
- [x] 运行时创建、资源依赖、重载、编辑器保存与 Prefab 覆盖迁移；复制后替换不影响原数据。
- [x] 迁移内置资产、生成工具和相关短测试；验证 Debug 编译，不运行大场景或 GPU/远端 CI。

## 边界

描述 GUID 和运行时组件 GUID 分离，已有 Prefab 属性键保持运行时属性身份并由场景层映射。
第一版只实例化 Camera、MeshRenderer；其他已注册描述可以编解码，但实例化明确返回 unsupported。
未知描述不静默丢弃。每种描述类型最多一项，数组位置不作为覆盖身份。

## 验收记录

2026-10-29：Windows Clang Debug 编译通过；39 项相关短测试通过，包括自定义描述往返、
未知/重复类型拒绝、复制隔离、省略字段保存、Scene/Prefab 重载、属性覆盖、编辑器保存和依赖边界。
修改过的 JSON 与 Python 文件语法检查通过。未运行大场景、GPU、浏览器构建及远端 CI。
