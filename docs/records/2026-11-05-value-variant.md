<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Value 通用 variant 访问与持久化验收

日期：2026-11-05。环境：Windows、windows-clang-debug。

## 完成范围

- 保留 value_kind 的分组编号与矩阵分支，显式映射存储下标，不再依赖枚举连续。
- value 通过 variant_traits 接入 variant_ops，统一分支访问、切换与序列化。
- 增加 monostate 空类型与矩阵编解码，矩阵按列主序逐元素编码。
- 删除专用 value codec，所有 variant 采用 $case/$content；对象分支复用通用多态引用。
- 删除 IPC 的旧 GUID 值编码适配，更新场景、Prefab、样例与测试数据。
- 资产解码支持通用非对象类型；标量实例访问不要求未选中的对象分支已注册，
  实际对象访问与完整类型注册仍校验其注册信息。

当前规则见 [Value 参考](../reference/value.md)、
[容器反射参考](../reference/reflection-containers.md)和
[ADR-105](../decisions/ADR-105-serialization-format-boundary.md)。

## 验证

- `cmake --build --preset windows-clang-debug -j5` 通过。
- 最终 55 项相关测试全部通过，覆盖 Value、序列化、Meta、资源加载、Scene/Prefab、IPC、
  编辑器、核心边界和内部头可见性。
- 核心边界负向测试独立通过，合计 56 项；首次运行发现的问题修复后已重跑对应测试。
- 覆盖全部 15 个 Value 分支、矩阵顺序与长度、非有限数值、空对象、非法分支及旧格式拒绝。
- 27 个修改 C++ 文件的 clang-format 检查及 git diff --check 通过。

未运行大型场景、远端 CI，未执行远端操作。
本次主动改变尚未发布的持久化格式，不提供旧 kind/value 或 $value 格式兼容。
矩阵属性在编辑器中只读显示，本次不增加矩阵编辑控件。
