<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-091：对象强引用入口统一

状态：已完成，2026-10-31。

按 [ADR-098](../decisions/ADR-098-object-reference-ownership.md) 实施：

1. 将现有 asset_ref 实现移入 Core ref，资源保留语义别名。
2. 统一反射编解码拥有者、不可变组件描述与编辑器整对象载荷。
3. 验证类型擦除共享控制块、移动/空值、弱缓存失效及销毁队列线程边界。

不要求值类型继承、不新增 GC、不自动序列化任意对象引用图。
验收为 Debug 编译及 Meta、组件编辑、资源生命周期相关 CPU 回归；不运行 GUI/GPU 或远端 CI。

## 验收结果

- Debug 全目标编译通过；新增 Core 头已纳入仓内接口头集合。
- 22 项相关 CPU 测试通过，覆盖资源释放线程、弱引用失效、类型擦除与成员别名共享控制块，
  以及 Meta、组件描述、编辑历史和资产解码回归。
- 未运行 GUI/GPU、大场景、浏览器和远端 CI。
- 通用 ref 属性的自动持久化不在本计划范围内，当前支持边界见 ADR-098。
