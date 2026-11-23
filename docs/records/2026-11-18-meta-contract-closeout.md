<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Meta 操作契约收尾审查

## 范围

本记录收尾 [PLAN-118](../plans/PLAN-118-error-contracts.md) P2，
接续读取、写入和视图查询三批实现。当前接口规则以
[反射参考](../reference/reflection.md)和[容器参考](../reference/reflection-containers.md)为准。

## 审查与改动

| 接口 | 结论 |
| --- | --- |
| reference reset | 适配器必备且无错误返回，改为 void；共用序列化遍历、value 写入和引用生命周期测试直接调用 |
| sequence resize/replace | 保留错误，存在长度限制、能力缺失、初始化回调拒绝 |
| associative insert/erase/clear | 保留错误，存在键校验、缺失及可选写能力 |
| optional reset/ensure | 保留错误，重置与默认创建能力可缺失 |
| variant select/index/value | 保留非法分支、不可默认构造及 valueless 的诊断 |
| member/as_*/property::get | 保留检查式查询，支持动态能力探测；不与直接读写有效借用的契约混淆 |
| 内部 from_access | 空存储由错误返回改为断言；索引及存在性在上层视图检查 |
| 外部 from_object | 保留空地址、注册冻结状态及对象类型检查 |

没有删除正常缺失、只读能力、错误类型、外部输入和自定义回调的拒绝路径。
没有新增 checked API，也没有把所有失败返回机械地改为断言。
适配器空存储检查与先前成员 getter 检查采用相同契约，借用本身不负责保活对象。

## 验证

Windows Clang Debug 与 MSVC Release 的八个测试目标构建通过，各九项 CTest 回归通过：
property_access、meta-access-contract、type_registry、serialization-archive、reflection-json、
meta-instance、meta-container、scene_description、prefab_property_override。
契约终止测试在两种配置均通过；引用保活、空引用、错误类型及覆盖应用回归保留。

Debug 的 gneiss_editor_ui 及其文档/模型依赖构建通过；未运行 GUI、GPU、静态库或跨平台验证。
修改行已格式化，git diff --check 通过。P2 本次收尾；全计划尚未完成。

## 剩余范围

P3 的 Render 回执身份、并发关闭和 Prefab 重放分类另行审查。
本记录不替代 P4 全计划验收，也不代表 GPU、跨平台或静态库矩阵完成。
