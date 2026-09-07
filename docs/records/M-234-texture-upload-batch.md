<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-234：渲染线程纹理 Upload Batch 与原子切换

## 结论

运行纹理的所选变体不再逐 Mip 调用直接写入。Granit Render Service 在渲染线程创建候选 Texture 后，
为该变体创建有界 Upload Batch，容量精确限制为变体声明的负载字节数和子资源数量；随后通过 Granit
Texture Asset 接口一次加入全部 Mip 并同步提交。

Granit 在复制前统一检查 Manifest、所选变体 Payload SHA-256、Mip 范围、Batch 空状态和容量，因此
摘要不匹配、布局错误或容量不足都不会留下部分命令。只有 Batch 提交成功后才创建 Texture View 并
把候选镜像交给缓存；任一步失败都会释放候选 View、Texture 和 Batch，CPU 侧 RID 所有权不变。

## 验证

- Windows Clang 严格警告下 Engine 与完整工程构建通过。
- Granit Platform Smoke 通过。
- Lantern Gallery Runtime Smoke 通过，覆盖 Cook 运行纹理的真实设备选择与整批上传。
- Editor 启动 Lantern Runtime 工作流通过。
- Granit 0.19 自身契约测试覆盖摘要损坏、Batch 背压和失败后零暂存命令。

异步跨帧 Mip Streaming 不属于本里程碑；当前首次创建仍同步提交完整 Mip 链。
