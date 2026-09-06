<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-183：标准 PBR Schema 与 Material 收敛

## 结果

Gneiss 后端投影改用 Granit 0.12 发布的 `pbr_standard.grmat`、参数名称、纹理标志和顶点 Attribute
位置。仓库内同构的 `gneiss_pbr.grmat` 已删除，构建时嵌入的 Material 与 Granit 标准资产逐字节一致。

Gneiss Material v3 仍只表达基础颜色、基础颜色纹理、金属度和感知粗糙度；其作者格式和 RID 没有
变化。

## 验证

- 当前 GPU 顶点布局通过 Granit 全 PBR 纹理变体检查。
- 缺少 UV 的布局被公共验证接口明确拒绝。
- Lantern Gallery、Granit 平台冒烟与 PBR Resolver 测试通过。
