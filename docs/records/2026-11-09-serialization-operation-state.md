<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 序列化操作状态分离

## 实现

context 不再内嵌路径、活动引用集合、reader 和递归计数；仅保存配置和操作期间的借用指针。
最外层 operation_scope 在栈上持有 operation_state，嵌套 codec 复用同一状态，
保持深度、路径与引用环检查；返回时恢复父层深度和 reader，最外层退出自动销毁状态。
不新增状态堆分配，不允许同一 context 并发使用。公开 property_path 在操作外返回空。
read/write context 和入口重命名仍待后续迁移。

## 验证

编辑器编译通过；serialization、serialization-archive、reflection-json 共 3 项 CPU 测试通过。
既有用例覆盖深度超限、递归 codec、引用环、后端错误与失败后复用。
补充断言验证初始、失败及成功返回后的 property_path 为空。
日志：build/plan108-operation-state-build.log、build/plan108-operation-state-tests-build.log。
git diff --check 通过。未执行 GPU、大场景或远端验证。
