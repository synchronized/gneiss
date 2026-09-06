<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-188：异步 GPU 契约与管线指标缺口确认

## 已确认契约

Granit 0.13 的异步操作使用 Renderer 所属的 64 位句柄，状态包含等待、运行、成功、失败和取消；
调用方非阻塞轮询、显式请求取消并销毁句柄。取消不保证撤销已经提交的 GPU 工作，Renderer 关闭前
必须清理其子操作。

该抽象当前服务于 Timestamp 异步读取，不是 CPU 任务系统，也不替代 Gneiss 的渲染线程和命令回执。

## 已确认缺口

Granit 0.13 的 RenderPipeline 指标 Slot 仍通过
`granit_timestamp_query_pool_get_results` 同步读取。新增的异步 Timestamp API 已覆盖 Vulkan 和
WebGPU，但尚未进入管线指标实现，因此跨后端应用无法只通过 RenderPipeline 获得统一指标行为。

## 上游建议

建议用户向 Granit 提交聚焦 PR：在每个 RenderPipeline Frame Slot 内持有至多一个异步读取操作，
Slot 复用前非阻塞轮询并发布最近完成样本，失败或取消时安全清理，Pipeline 销毁时处理全部操作后
再销毁 Query Pool。保持现有 `granit_render_pipeline_get_metrics` ABI 不变，并用同一契约测试覆盖
Vulkan、WebGPU、能力不足、未就绪、取消、失败和关闭。

Gneiss 不应复制私有管线或直接访问 RenderPipeline 内部 Query Pool；上游合并发布后再完成接入。
