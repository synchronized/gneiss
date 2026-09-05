<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-169：渲染性能观测

## 结果

渲染线程现已提供内部性能快照。Frame Packet 捕获记录单调时钟耗时和实际复制的资源、实例、UI 与
Debug Draw 有效载荷字节；每个完成回执记录排队、资源准备、Acquire、录制提交、Present 和渲染线程
总耗时。队列快照同时记录当前任务构成、高水位及 Frame/Command 生命周期累计计数。

Application 在渲染服务关闭前读取最终快照，并通过 `granit.render.performance` 诊断事件输出摘要。
统计接口保持在 Granit 后端内部，不改变公共 C ABI，也不泄漏 Granit 类型。

## 验证

- `gneiss.render_frame_packet`：验证构造耗时和复制字节随自有帧包保存。
- `gneiss.render_executor`：验证提交、执行、替换、队列深度、排队耗时和 Command 计数。
- Windows Clang Debug 相关目标在警告视为错误条件下构建通过。
- `git diff --check` 通过。

## 边界

- 复制字节表示容器中的有效载荷，不估算分配器和哈希桶额外开销。
- 阶段耗时是 CPU 单调时钟区间，不代表 GPU 内部执行百分比。
- 当前保存最近值、最大排队值和累计计数；长期直方图与性能面板不在本里程碑范围。
