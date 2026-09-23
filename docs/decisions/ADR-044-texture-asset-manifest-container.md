<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-044：运行纹理封装承载 Granit Manifest 与负载

- 状态：已接受
- 日期：2026-09-07
- 取代：[ADR-043：KTX2 作为运行时纹理容器](ADR-043-runtime-texture-container.md)

## 背景

0.34.0 使用单变体 RGBA8 KTX2 作为运行表示。Granit 0.19.0 已提供 Texture Asset Manifest、设备
变体选择和逐 Mip Upload Batch，但 Manifest 本身不包含像素负载，也不负责文件与资源生命周期。
直接把 Granit Manifest 当作完整资产会丢失负载边界；继续只用 KTX2 则需要 Gneiss 重复实现变体
选择和上传校验。

## 决策

- 0.35.0 的 Cook 产物使用 `.gneiss-texture`，由固定 64 字节小端 Header、Granit Texture Asset
  Manifest v1、零填充对齐和连续 Payload 区组成。
- Header 使用独立 Magic 和 Gneiss Schema，记录 Manifest 与 Payload 的偏移、大小及完整文件大小；
  两个区域不得重叠，保留字段与对齐填充必须为零，文件不允许尾随数据。
- Manifest 中的 `payload_offset` 相对 Payload 区起点。一个文件携带目标平台所需的有序变体；其
  SHA-256、Mip/层布局和 GPU 格式规则由 Granit 编码及检查接口维护，Gneiss 不复制这些字段。
- Gneiss 读写器只验证外层封装并返回借用的 Manifest/Payload 字节视图；仅 Granit 适配层包含
  Granit 头文件并执行 Manifest 检查、设备选择和 Upload Batch 写入。
- PNG 与 KTX2 继续属于作者输入或交换格式。0.35.0 发布包必须重新 Cook；0.34.0 包继续由对应
  Runtime 使用，不在新 Runtime 中保留旧 KTX2 发布产物兼容分支。
- 文件读取、缓存键、平台策略、RID、渲染线程调度和失败时保留旧资源仍由 Gneiss 负责。

## 影响

- Gneiss 可以在不修改自身外层 Schema 的情况下采用 Granit 后续兼容 Manifest，并避免重复维护
  GPU 格式布局与摘要校验。
- Granit 仍不理解 Gneiss URI、包结构或资源所有权，第三方类型不会进入公共 API。
- 运行纹理比裸 KTX2 多一层轻量封装；工具和 Runtime 必须同时升级，旧工程需重新 Cook。
- 未来分离 Payload 或流式文件需要新增 Gneiss Schema，而不是改变 v1 偏移语义。
