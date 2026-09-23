<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-232：平台纹理变体构建、缓存键与包内容

## 结论

PNG 资产会先解码为 RGBA8 并生成直到 1×1 的完整 Mip 链，再构建为单个
`.gneiss-texture`。产物包含 Gneiss 版本化外层 Header、Granit Texture Asset Manifest、BC7
优选变体及 RGBA8 回退变体。两种变体保持相同尺寸、颜色空间和 Mip 集合，Manifest 顺序固定为
BC7、RGBA8。

每个变体负载及逻辑内容标识均使用工具侧 SHA-256。处理器版本已提升为 3，目标平台、架构、构建
配置、颜色空间及处理器版本继续参与缓存键；旧 KTX2 Cook 缓存不会被误用。JSON URI、构建输出和
包清单统一使用派生 `.gneiss-texture` 路径，包内不保留作者 PNG 或 BC7 编码器。

## 验证

- 标准 SHA-256 空输入与 `abc` 测试向量通过。
- 相同完整 Mip 链重复构建得到逐字节相同的运行纹理。
- Gneiss 外层解码和 Granit Manifest 检查均能读取构建结果。
- 测试产物按顺序包含 BC7 SRGB 与 RGBA8 SRGB 两个变体及完整子资源表。
- 资产构建、缓存复用、依赖传播、Shipping 裁剪和 AssetC Cook 测试通过。
- Windows Clang 严格警告构建通过。

本记录只确认离线构建与包内容。设备能力选择和 Runtime 消费由 M-233、M-234 完成。
