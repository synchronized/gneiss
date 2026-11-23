<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Value 对象强引用验收

按 [ADR-104](../decisions/ADR-104-value-object-reference.md) 增加对象强引用分支，
复用 ref 与 meta::object；不引入任意对象值容器或新的引用计数实现。
Meta 引用类别保持不变，引用属性接通 value 读写，保留 const 及类型安全边界。

IPC 两端显式拒绝对象载荷；通用 value JSON 返回 unsupported。
组件/材质编辑和 Prefab 属性覆盖拒绝把对象引用当作标量快照。
现有类型化 ref 仍正常内联序列化。

Windows Clang Debug 全量构建、28 项相关测试通过。新增测试覆盖共享修改、空引用、
最后引用释放、多继承指针调整、错误类型写入不改变旧引用、const 保护，
value JSON 拒绝及类型化引用往返、组件/材质编辑与 Prefab 覆盖拒绝。
相关反射、IPC、编辑器和场景测试通过；clang-format 与 git diff --check 通过。

未运行大场景、完整平台矩阵或远端 CI。未实现对象 value 的持久化、深拷贝、
循环回收或只读对象引用分支。完整接口见[通用值](../reference/value.md)。
