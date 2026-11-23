<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 注册入口收敛

## 范围

2026-11-09，继续 [PLAN-108](../plans/PLAN-108-meta-serialization-api.md) 第 4 阶段。
C++ 对象注册器统一为 registration<T>，迁移内建注册、资源注册和所有调用，不保留旧类型别名。
默认与显式 registry 构造沿用既有机制；只有 commit 提交，重复提交拒绝。
新增 name/guid 设置供没有静态身份的外部类型使用，名称自有，traits 冲突仍拒绝。
字段继续使用显式 property_id 与编译期成员指针，不引入自动身份或运行期捕获状态。

复用 meta/ops/sequence_ops.hpp 的公开 sequence_traits，新增自定义序列测试，
验证成员访问、元素修改、越界和 JSON 编解码往返；消费者不直接包含 detail/type_ops.hpp。
未新增容器注册框架、注册表或类型擦除状态。

## 验证

Windows Clang Debug 默认构建通过，日志 build/plan108-registration-build.log。
40 项定向 CPU 回归全部通过（60.25 秒），日志 build/plan108-registration-tests.log。
新增测试验证显式名称/GUID、traits 冲突、名称寿命、schema 版本及自定义序列。
clang-format 差异检查和 git diff --check 通过。
未运行 GPU、大场景、完整平台矩阵或远端 CI。

## 剩余范围

第 4 阶段尚未完成：普通对象无 GUID 注册、冻结索引和内部访问边界仍需继续处理。
后续策略、上下文及文件布局迁移未在本批实现；PLAN-108 与 DEV-052 均未完成。
