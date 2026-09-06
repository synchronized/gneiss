<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-184：资产目录与打包路径统一

## 结果

PACKAGE、FetchContent 和父工程目标均只通过 Granit 0.12 发布的
`granit_RENDER_PIPELINE_ASSET_DIR` 定位标准资产。Gneiss 不再从 Granit target 的源码目录反推路径。

标准 PBR Shader、sidecar 和 Material 会同时进入 Gneiss 内嵌数据、构建树暂存目录和安装树；安装
消费测试会检查七个文件全部存在。

## 验证

- Windows Clang FetchContent 配置和完整构建成功。
- 安装后的稳定 Runtime Consumer 及标准资产完整性检查通过。
- PACKAGE、父工程目标及跨平台组合将在 M-186 完整矩阵中继续验证。
