<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-092：反射引用属性访问

状态：已完成，2026-10-31。

接续 [PLAN-091](PLAN-091-object-reference-ownership.md)，补齐 ref<T> 属性的运行期访问：

1. reference 独立于 object/optional；element_type 描述静态目标类型。
2. 提供显式借用解引用，空引用返回 not_found，ref<const T> 拒绝可写访问。
3. 同类型引用 assign 共享控制块，reset_reference 仅释放当前引用；保持原删除器。
4. 默认反射 JSON 对引用字段返回 unsupported，不展开引用图、不隐式创建目标。

不提供动态派生类型推断、自动引用图持久化、编辑器引用选择控件或强制基类。
引用目标视图继续借用，不跨越引用替换/重置或目标地址失效；调用方负责同步。
验收：Debug 编译以及 Meta 和现有组件/资源相关 CPU 回归，不运行 GUI/GPU 或远端 CI。

## 验收结果

Debug 全目标编译通过，22 项相关 CPU 回归通过。新增测试覆盖引用属性注册与类型查询、
空引用、共享目标修改、同类型替换、const 目标访问限制、重置及默认 JSON 编解码拒绝引用。
未运行 GUI/GPU、浏览器、大场景或远端 CI。
