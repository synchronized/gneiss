<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-043：KTX2 作为运行时纹理容器

## 状态

已被 [ADR-044：运行纹理封装承载 Granit Manifest 与负载](ADR-044-texture-asset-manifest-container.md)
取代。

## 背景

Runtime 当前从 Texture JSON 间接读取 PNG 并在启动路径解码，无法携带完整 Mip、目标格式或稳定的
离线构建结果。Granit 负责 GPU 资源与上传，不应理解 Gneiss 的资产容器。

## 决策

- Gneiss 工具链把作者纹理转换为受约束的 KTX2；Runtime 只接受该运行表示。
- 首版只接受二维、单层、单面、无超级压缩的 RGBA8 UNORM/SRGB KTX2，并要求完整 Mip 链。
- KTX2 解析、Schema 校验、资产 URI 和导入设置由 Gneiss 持有；Granit 只接收格式、维度和各 Mip
  字节范围。
- 作者格式解码与 Mip 生成只存在于工具目标，不能进入 Runtime 公共依赖。
- 后续块压缩在 Granit 公共能力查询支持后按设备/平台选择，未知或不支持格式显式失败。

## 影响

- 发布包不再依赖作者图像格式解码器，启动路径更确定，也能直接上传预生成 Mip。
- 开发构建增加私有工具依赖与 Cook 时间，但可由内容缓存复用。
- RGBA8 KTX2 首版不会减小纹理体积；它先解决运行表示、Mip 和边界稳定性，压缩另行演进。
