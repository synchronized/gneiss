<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 非标量属性整项绑定验收

日期：2026-11-04。分支：feature/0.52-architecture-cleanup。

## 实现

扩展现有 property<Getter, Setter>，非标量 getter 必须返回稳定 const T&，setter 对完整候选值
进行校验与替换。绑定不提供可写成员地址，反射整项赋值调用 setter，JSON 解码先完成独立候选。
新增内部 property_view_access 负责临时值寿命与替换；不会将候选视图暴露为可写业务成员。
具体接口和失败边界见 [反射参考](../reference/reflection.md)。

object_description.components 已注册此绑定。component_descriptions::replace 集中校验非空、
类型唯一及替换，IPC 也复用该校验。删除普通对象 codec 的组件数组读写分支；无 prefab 的对象
编码直接返回反射结果。Prefab 包装仍保留，输出格式不变，不引入 optional 反射。

## 验证

受影响 Engine、Meta、Scene/Prefab、IPC 和编辑器测试目标 Windows Clang Debug 编译通过。
18 项相关 CTest 全通过（2.06 秒）；git diff --check 通过。
新增测试覆盖：序列/嵌套对象绑定、拒绝可写子视图、成功整项替换、类型错误、解析失败不调用
setter、setter 拒绝保留旧值、组件空项/重复类型拒绝，以及复制快照不受替换影响。
既有 Scene/Prefab 往返及编辑器组件编辑/撤销测试通过。

## 边界

getter 不返回临时值；setter 负责自身失败原子性，Meta 不回滚任意回调副作用。
只承诺受约束属性整体替换，不承诺 deserialize_instance 对整个父对象自动回滚。
未运行全目标、静态构建、跨平台矩阵、大场景或远端 CI。prefab 字段迁移不属于本批完成范围。
