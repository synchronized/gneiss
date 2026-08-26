<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-16 Mesh 与 Material Loader 实施记录

- 日期：2026-08-26
- 环境：Windows、Clang、C++20、yyjson 0.12.0
- 结果：完成 VFS 到 Render RID 的同步加载与缓存租约闭环

## 实施结果

- 定义最小 Mesh Triangle List 与固定颜色 Material JSON v1。
- Loader 通过 VFS 读取，严格校验后调用现有 Render Resource Service 创建 RID。
- 拥有型租约让场景只借用 RID；缓存命中复用 RID，引用归零并清理缓存后按类型销毁。
- 覆盖 Mesh/Material 成功、重复加载、类型冲突、语法失败、失败后重试和释放顺序。
- 增加三角形 Mesh 与 Material 资产；示例切换和场景实例化留在 M-17 完成。

长期格式决策见 [ADR-007](../decisions/ADR-007-render-asset-formats.md)，字段定义见
[Mesh 与 Material 资产格式 v1](../reference/render-asset-formats.md)。
