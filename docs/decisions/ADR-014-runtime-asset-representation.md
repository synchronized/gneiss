<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-014：源资产、作者格式与 Runtime 二进制资产分层

- 状态：已接受
- 日期：2026-08-27

## 背景

真实 Lantern glTF 导入后产生约 1.9 MiB 的 Mesh JSON。Debug 启动中，文本解析、展开顶点和资源
创建成为主要加载成本。未来编辑器仍需要可迁移、可审查的数据，而 Runtime 需要快速校验和顺序读取，
两者不应被迫使用同一物理格式。

## 决策

- 外部 glTF、PNG 等是源资产；作者 JSON 面向编辑、测试和版本迁移；Runtime 使用 Gneiss 自有的
  版本化二进制资产。
- Import IR 是源格式与 Runtime 格式之间的唯一转换边界，不让 fastgltf 或二进制布局进入公共 ABI。
- 二进制格式使用固定小端序、定宽字段、文件相对 Offset 和严格边界校验，不持久化 C++ 结构体、
  指针或后端句柄。
- 首个格式只覆盖 Mesh；Material 与 Scene 数据较小且仍频繁演进，继续使用严格 JSON。
- `gneiss_assetc inspect/validate/dump` 提供摘要、验证和按需 JSON 展示；Debug JSON 不是 Runtime 输入。
- 单资产格式稳定后再评估 Pack，不预先设计通用大容器。

## 影响

Mesh 文件可保留索引并避免 Runtime JSON 解析。索引渲染边界后续由
[ADR-015](ADR-015-indexed-rendering-boundary.md) 确立，不需要改变 Mesh Binary v1。旧
`.mesh.json` 继续兼容，用于已有示例、测试和人工创作，但离线导入默认生成 `.gneiss-mesh`。

## 替代方案

- 所有资产继续使用 JSON：便于查看，但大型几何解析慢、体积大且无法直接保留紧凑数据。
- Runtime 直接读取 glTF：减少自有格式，但把外部格式复杂度和第三方依赖带入运行时。
- 立即采用 FlatBuffers 或通用容器：工具成熟，但当前只需固定布局 Mesh，增加了 Schema 与依赖成本。
