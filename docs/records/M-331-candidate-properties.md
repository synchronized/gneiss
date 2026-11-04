<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：候选属性写入边界

按 [ADR-066](../decisions/ADR-066-candidate-properties-and-notifications.md) 在同一内部反射注册表
增加可选候选 setter，支持只绑定候选能力。注册遵循原有冻结、重复绑定一致性及只读字段约束；
调用复用字段类型和值校验，异常返回 INTERNAL。候选写入不调用普通 setter、不发业务通知。
候选数据类型和独占访问由内部调用者保证；回调不得产生外部副作用，失败候选由事务丢弃。

Windows Clang Debug 构建通过。reflection-boundary、core-boundary、core-boundary-negative
3 项通过；后续 C/C++ reflection、property_access、component_reflection、reflection-boundary
5 项回归通过。新增测试覆盖冻结前拒绝、候选独立写入、类型错误、回调失败与异常、空候选、
缺失能力和不回退到带副作用的运行时 setter。定向 clang-tidy 已运行并修正新增初始化项告警，
既有格式告警未作无关清理。

本轮未改变公共 C/C++ 接口。内置组件候选适配、Prefab 使用者与发布后通知仍未接入，
不能描述为信号系统或完整事务已实现。未验证真实窗口、GPU、跨平台或静态安装矩阵。
