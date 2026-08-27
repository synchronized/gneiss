<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# TOOL-002 Mesh Binary v1 实施记录

## 结论

2026-08-27 在 Windows Clang Debug、Granit Vulkan 平台路径上完成 Lantern 灯廊对比。Mesh Binary v1
显著降低导入 Mesh 体积和同步场景加载时间，旧 JSON Mesh 兼容测试保持通过。

## 数据

| 指标 | Mesh JSON v3 | Mesh Binary v1 | 变化 |
| --- | ---: | ---: | ---: |
| Lantern 三个 Mesh 总大小 | 1,918,762 B | 197,608 B | -89.7% |
| Scene 与资产加载 | 1,140.9 ms | 113.0 ms | -90.1% |
| 三帧 Smoke 总启动 | 约 1.5 s | 约 0.62 s | 约 -59% |

数据来自同一开发机的单次 Debug 诊断，不代表跨平台基准。Binary 文件保留 4,145 个唯一顶点和
16,182 个 UInt32 索引。本记录测量时 Loader 仍会展开 Triangle List；后续 TOOL-003 已贯通索引
渲染，因此这里的性能数字只代表 TOOL-002 当时的结果。

## 验证范围

- Encoder/Decoder 往返、确定性文件和 AABB。
- 错误 magic、版本、Offset、截断、非有限顶点和越界索引。
- `gneiss_assetc inspect`、`validate`、`dump --format json`。
- glTF 导入、Runtime Scene/Mesh/Material/Texture 加载及 Granit 图形 Smoke Test。
- 旧 Mesh JSON v1/v2/v3 Loader 兼容路径。
