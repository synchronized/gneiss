<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Meta 写入与候选替换契约

## 范围

接续 [PLAN-118](../plans/PLAN-118-error-contracts.md) P2 的读取链。
当前契约以[反射参考](../reference/reflection.md)为准；本批不关闭整个 P2。

## 审计与处理

- instance 的 assign(value)、assign(instance)、assign_object 和 replace 要求有效目标借用；
  实例复制还要求有效来源。空实例属于调用违约，改为断言，不返回可恢复 invalid_state。
- Registry setter 保留输入内容、类型匹配、空擦除指针、访问能力和业务拒绝的诊断。
  这些错误来自实际输入或适配器策略，不能因调用发生在内部就删除。
- replace 的 object_construct 返回独立非空候选。内置注册使用 make_ref，分配失败抛异常；
  自定义构造器返回空值视为违约，不再误报 out_of_memory。
- serialization/value.cpp 与 traversal.cpp 的候选写入仍保留初始化/提交失败及路径传播。
  初始化失败不调用 setter；setter 自身依照既有契约先验证再修改。
- Prefab 属性覆盖继续先验证身份、路径和值，再对候选应用；输入拒绝和失败保持语义保留。
  value_edit/prefab_replay 中的重放错误分类仍归 P3，不借本批改成进程终止。

## 测试维护

旧空实例 assign 返回 invalid_state 的测试退出，合并到 meta-access-contract 子进程验证。
沿用上一批的退出码与诊断检查，不创建新的测试可执行文件；覆盖空目标、空来源和既有读取违约。
候选初始化回归验证：临时序列已扩容后返回 not_ready，原集合和 setter 调用次数均保持不变。
既有类型不匹配、setter 拒绝、解码失败与 Prefab 覆盖用例继续保留。

## 验证

- Windows Clang Debug 五个相关目标构建通过，六项定向回归通过：value_edit、property_access、
  meta-access-contract、reflection-json、meta-instance、prefab_property_override。
- Windows MSVC Release 两个相关目标构建通过，property_access、meta-access-contract、
  meta-instance 三项回归通过。空目标/来源断言在两种配置下均按预期退出。
- git diff --check 通过。本批未运行全仓、静态库、跨平台或 GPU 矩阵。

## 后续

容器 size、optional/reference has_value 和 reference owner 等包装的 result 没有底层失败来源，
后续应连同序列化、面板和测试调用者一起迁移。索引越界、空引用解引用、variant valueless、
可选能力缺失及自定义替换拒绝仍需诊断；不能机械删除容器视图的全部 result。
