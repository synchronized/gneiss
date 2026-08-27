<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-27 图片解码器 Spike 记录

- 日期：2026-08-27
- 候选：libspng 0.7.4、stb_image 2.30、Wuffs、LodePNG
- 结论：选择 libspng 0.7.4，并以 miniz 3.1.2 提供 Deflate

## 对比结果

| 候选 | 许可证 | 当前用例适配 | 错误与安全 | 构建影响 | 结论 |
| --- | --- | --- | --- | --- | --- |
| libspng 0.7.4 | BSD-2-Clause | 聚焦 PNG、内存 RGBA8、Gamma | 稳定错误码、OSS-Fuzz | C 源码；配合 miniz | 采用 |
| stb_image 2.30 | Public Domain/MIT 双许可 | 格式远超当前范围 | 简单错误文本，官方提示偏可信资产 | 单头文件 | 不采用 |
| Wuffs | Apache-2.0/MIT 双许可 | PNG 能力完整 | 生成 C 具备内存安全目标 | 生成代码与低层 API 较重 | 暂不采用 |
| LodePNG | Zlib | 聚焦 PNG、无外部依赖 | 数字错误码 | 两个源码文件但无稳定标签 | 不采用 |

## 版本与接入

- libspng `v0.7.4` 对应提交 `5c2183e3e81bf9d989dce3162cd903f986ef9c6e`。
- miniz `3.1.2` 对应提交 `77d0dce8627735138c51770d1799a1ef48f2117d`。
- miniz 使用 MIT 许可证；只作为 libspng 的内部实现依赖。
- 不把解码器头文件、target、宏或错误枚举传播到 Gneiss package 的普通接口。

## 验收重点

- 从 VFS 字节解码 RGBA、RGB、灰度和带 Alpha PNG，统一输出 RGBA8。
- 在分配前拒绝零尺寸、尺寸乘法溢出和超过 256 MiB 的输出。
- 覆盖截断签名、损坏 chunk、CRC 错误和未知格式；失败不得污染资源缓存。
- 保留 PNG sRGB/Gamma 是否存在的信息；首版由 Texture 源描述明确最终颜色空间。
