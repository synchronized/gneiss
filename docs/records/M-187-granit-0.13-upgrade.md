<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-187：Granit 0.13.0 升级与兼容审计

## 结果

Gneiss 的 Fetch 默认提交已升级到 Granit 0.13.0 发布提交
`97800369f1865ce6d7668f23d7085b4d2c5f5f7d`，PACKAGE 与导出依赖最低版本同步提升至
0.13。旧 0.12 默认缓存会自动迁移，用户显式提交覆盖不被改写。

Granit 0.13 没有改变 Shader、Material 和 Environment 持久化格式，Gneiss 0.28 建立的标准资产
投影无需迁移或重新生成。

## 验证

- Windows Clang Shared Debug 完整构建及 132/132 测试通过。
- Granit 默认版本缓存迁移、平台烟雾和安装后稳定 Runtime Consumer 通过。
- 标准 PBR Shader、Material 与 RenderPipeline 资产安装检查通过。
