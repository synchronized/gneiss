<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-182：Shader Asset 公共检查接口接入

## 结果

PBR Shader Resolver 现通过 `granit_shader_asset_inspect` 验证清单并取得内容 ID，不再读取 Granit
私有魔数、固定摘要偏移或最小字节数。磁盘资产和内嵌资产共用同一检查路径，检查失败时清空候选
状态并返回错误。

## 验证

- Vulkan SPIR-V 与 WebGPU WGSL 能按检查后的内容 ID 解析。
- 未知内容 ID、错误 Profile、缺失 sidecar 和损坏清单均返回失败且 Resolver 不保持半初始化状态。
