<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 结构化 value 迁移前的标量边界验收

日期：2026-11-07。所属计划：[PLAN-104](../plans/PLAN-104-value-document-prefab-overrides.md)。

## 变更

增加 is_scalar_value_kind 显式分类，Meta 类型推导、基础类型发现和属性值校验统一使用它。
新增结构化分支不再因“不是 object”而自动成为标量。基础值列表、分支编号及持久化格式未改变。
编译期检查覆盖对象、invalid、未定义编号拒绝以及数学值/GUID 接受。

## 验证

Windows Clang Debug 完整构建通过。12 项 value、Registry、Builder、Meta ops/instance/container、
JSON/Archive、组件描述、Prefab 属性覆盖和组件编辑回归全部通过；git diff --check 通过。
日志 build/value-scalar-boundary-build.log、build/value-scalar-boundary-tests.log 不入库。
未运行 GPU 大场景、跨平台或远端 CI。

## 限制

本批只完成标量分类前置清理；不可变结构化值、捕获恢复、稳定组件身份、操作式覆盖和文档版本均未实现。
现有 property_snapshot、子树及文档历史路径仍保留，必须在后续对应阶段完成替代后删除。
