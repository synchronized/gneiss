<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-185：跨后端契约与失败语义验证

## 结果

标准 PBR 资产测试覆盖 Vulkan SPIR-V、WebGPU WGSL、标准 Material 字节一致性及公共顶点布局检查。
这只验证后端中立资产契约，不代表 Gneiss 已提供 WebGPU Runtime。

缺失 sidecar、损坏清单、未知内容 ID、错误 Profile 和缺失 UV 均有确定失败结果；候选 Resolver
失败后保持无效，不影响已由渲染服务持有的有效投影。
