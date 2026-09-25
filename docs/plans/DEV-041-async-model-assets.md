<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# DEV-041：异步模型资产开发计划

实施中。版本范围与门禁以 [VER-041](VER-041-0.41.0-async-model-assets.md) 为准。

1. 先接通 Granit 离屏输出和 GPU Readback，建立可失败的像素验证；复用真实 Gneiss 场景与资源路径。
2. 提取 Mesh/Material 的纯 CPU 准备，解析材质依赖并建立有界源快照，复用既有格式校验。
3. 扩展现有加载服务与候选事务；主线程阶段不读取磁盘，验证旧身份，保留材质依赖所有权。
4. 扩展渲染执行器候选准备，预算限制一次推进的资源数/字节，失败回收与整批发布分离。
5. Runtime 混合批次接入异步入口；IPC 传递有身份的阶段与取消请求，Editor 提供状态和重试。
6. 覆盖事务失败、加载退出与真实模型重导入，测量窗口响应；同步更新指南与验收记录。
7. 版本号、Changelog、本地门禁、一个 PR、手动矩阵、合并、标签与 Release。

## 主要落点

| 模块 | 工作 |
| --- | --- |
| `src/render/render_asset_loader.*` | 纯准备、依赖与原子资源发布 |
| `src/asset/texture_load_service.*` | 扩展为渲染资产批次服务，复用生命周期和宿主注入 |
| `src/render/granit/granit_render_service.*` | GPU 候选、上传预算和回读 |
| `src/application/application_asset_reload_internal.*` | 内部混合资产请求、状态与测试接入 |
| `apps/runtime/`、`apps/editor/` | 修订、IPC 进度、取消及可见反馈 |
| `tests/`、`tools/performance/` | 两模式事务、真实图形像素与模型负载 |

线程、所有权与失败语义先于性能优化收口；每阶段完成连贯回归后本地提交。接口保持内部 C++，
不增加不稳定公共 C ABI。新增长期取舍写入 [ADR-049](../decisions/ADR-049-model-asset-transactions.md)。
