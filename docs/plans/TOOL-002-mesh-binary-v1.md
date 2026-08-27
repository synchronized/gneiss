<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# TOOL-002：Mesh Binary v1 最小闭环

## 状态

- 状态：已完成
- 优先级：P1
- 前置条件：TOOL-001 glTF 资产编译器

## 背景与目标

将 glTF Import IR 确定性烘焙为紧凑、可严格验证的 `.gneiss-mesh`，同时保留旧 JSON Loader 和
按需 Debug JSON，降低真实场景启动时间并为后续索引绘制建立稳定输入。

## 非目标

- 通用资产包、压缩、加密、流式加载或内存映射。
- 冻结 Material、Scene、Texture 或编辑器作者格式。
- 在本任务修改公共 Render ABI 或实现 GPU 索引绘制。

## 已确认决策

- 格式遵循 [ADR-014](../decisions/ADR-014-runtime-asset-representation.md)，固定小端序和 16 字节块对齐。
- v1 顶点固定为 Position Float3、UV Float2、Normal Float3，索引固定为 UInt32。
- Header 是 80 字节，包含 magic、版本、数量、步长、Offset、文件大小和 AABB。
- 导入默认写出二进制；旧 `.mesh.json` 继续由 Runtime Loader 支持。

## 实施顺序

1. 定义并记录 Mesh Binary v1 Header 与数据布局。已完成。
2. 实现独立 Encoder/Decoder，覆盖溢出、截断、版本、有限值、单位法线和索引边界。已完成。
3. 让 `gneiss_assetc import` 写出二进制 Mesh，并让 Runtime Loader 自动识别。已完成。
4. 增加 `inspect`、`validate` 和 `dump --format json`。已完成。
5. 切换 Lantern 示例，记录体积与加载时间差异。已完成，见
   [实施记录](../records/TOOL-002-mesh-binary-v1.md)。

## 测试与验收

- 相同输入逐字节一致；Header 和数据块满足规范。
- 成功往返保持顶点、索引和 AABB。
- 错误 magic、版本、Offset、截断、非有限数值和越界索引明确失败。
- 旧 JSON Mesh 测试保持通过，Lantern 图形 Smoke Test 通过。
- `dump` 的 JSON 仅用于查看，不能被 Runtime 当作二进制输入。

## 风险与未决问题

- Loader 当前仍按索引展开顶点；真正减少 CPU/GPU 内存需后续扩展 Render Service。
- v2 是否增加切线、子网格或 UInt16 索引，由后续 PBR 与实际资产统计决定。
