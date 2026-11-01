<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-080：统一反射类型查询与属性类型

状态：已完成

## 已确认范围

- 增加 type_of<T>()、find_type(type_id/guid)，默认注册表和显式独立注册表使用相同规则。
- 查询要求注册表冻结。内建值使用已有静态操作表；对象与枚举必须登记。
  容器模板查询检查叶子类型；运行期 ID 查询只发现已登记属性引用的容器，不隐式登记新类型。
- property_info 收敛为身份、名称、标记、读写能力和 type；删除重复的类别、GUID、枚举选项字段。
- 面板与序列化通过 type 判定类型，instance 继续负责实际读写，不绕过 getter/setter。
- 保留 type_info 快照，不扩展多态指针、动态构造和独立枚举 GUID 规则。

## 验收

完整 Debug 构建；类型查询的冻结/未知/失效边界，模板与 ID/GUID 一致性，枚举与容器类型，
视图保活，访问器、序列化、属性面板和内部头文件边界回归。不运行大场景、GPU 和远端 CI。

## 实施结果

已补齐模板与 ID/GUID 查询，property_info 改为单一 type 字段，面板、JSON 序列化和
Prefab 属性校验已迁移。手工 getter/setter 保留原读写路径，type_info 继续作为快照。
属性快照中的类型延长注册表寿命；测试验证清空所有快照和视图后注册表正常销毁。

2026-10-30：完整 Windows Clang Debug 构建通过，26 项相关 CPU 测试通过，包含
类型/属性注册、反射 JSON、容器、默认注册表成功与失败、序列化、Scene/Prefab、
材质面板及内部头文件/分层边界。未运行大场景、GPU、浏览器或远端 CI。
