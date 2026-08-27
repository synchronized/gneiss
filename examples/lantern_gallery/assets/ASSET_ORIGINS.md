<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 示例资产来源

`source/Lantern.glb` 下载自 Khronos Group 的 `glTF-Sample-Assets` 仓库：

- 资产名称：Lantern
- 上游地址：<https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Lantern>
- 下载地址：<https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/Lantern/glTF-Binary/Lantern.glb>
- 下载日期：2026-08-27
- SHA-256：`a79458c4b02d695187a952f23a63b8bf278e7bc3d316a3c2a314f2d6974181f1`
- 模型及其二进制、图像资产许可证：CC0-1.0

上游许可原文保存在 `source/LICENSE.md`。场景、地面、石柱、相机与输入映射由 Gneiss 项目原创，
按仓库 MIT 许可证分发。构建过程通过 `gneiss_assetc` 将原始 GLB 转换到构建目录，再叠加原创场景
描述；生成产物不作为源文件提交。

`authored/textures/image-0.png` 是从 GLB 内嵌的 2048×2048 基础颜色 PNG 确定性缩小得到的
512×512 派生版本，用于缩短示例启动时间。处理命令使用 ImageMagick 7 的 Lanczos 滤镜、移除元数据
并设置 PNG 压缩等级 9；SHA-256 为
`7152110992aba89e919ef41ba384a77b3636465b1b023255e917aab20a30ce0c`。派生图继续遵循上游
CC0-1.0 许可证。
