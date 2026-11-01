<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景工程模板

复制此目录，或使用 `gneiss_project create <目录> <名称>` 创建工程。
使用 `gneiss_runtime --project <目录>` 加载场景；不需要编译游戏 DLL。

`assets/` 保存场景与资产，`sources/` 保存导入源文件。`asset_build.retain` 声明发布时额外保留的资产。
`gneiss_project package` 打包 Runtime、动态依赖和资产，不包含游戏代码。
客户 system 注册与脚本接入尚未设计。
