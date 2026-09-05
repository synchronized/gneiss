<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-038：渲染线程独占 GPU 对象并消费不可变任务

- 状态：已接受
- 日期：2026-09-05

## 背景

当前 Application 主循环同时处理窗口事件、逻辑更新和 Granit GPU 工作。资源上传、帧槽等待或
Present 阻塞时，Editor 与 Runtime 的事件处理也会暂停。Granit 0.7.0 Model Viewer 已验证“主线程
生产不可变帧包、桌面渲染线程串行消费”的可行性，但其执行器属于示例私有代码，不是公共能力。

## 决策

- Gneiss 在 Granit Render Service 内建立自己的执行边界，不复制示例 API，也不要求 Granit 接管
  Application 线程模型。
- Application 主线程拥有事件、输入、Scene/ECS、动画采样和 UI 构建；渲染线程独占 Renderer、
  Swapchain、Pipeline、GPU 资源镜像、Upload Batch、提交和 Present。
- 主线程提交的 Frame Packet 拥有全部逐帧可变值。稳定资源以 RID 与修订引用；渲染线程不得回读
  Scene Tree、ECS、ImGui 或主线程下一帧会修改的内存。
- Frame 可以在执行前被更新帧替换，以限制交互延迟；资源及控制 Command 不可丢弃，按提交顺序执行。
  两类任务共享确定序列号，并分别产生完成回执。
- 队列保持有界。命令容量耗尽返回 `not_ready`；不阻塞窗口事件线程等待空位，也不无限分配内存。
- 资源变化先准备自有 CPU 数据，再由渲染命令创建或上传 GPU 投影；只有成功回执才能发布新投影，
  失败继续使用旧投影。
- Resize、Surface Lost、Pipeline 重建和关闭通过排空屏障串行化。关闭时渲染线程先释放所拥有的 GPU
  对象，再停止并 Join；任何线程都不得在 Join 后访问执行器状态。
- 同步执行器保留为测试基线及不支持独立渲染线程的平台实现，两种执行器消费相同任务契约。

## 影响

- GPU 或 Present 等待不再直接阻塞窗口事件和逻辑更新，Editor 与 Runtime 共用一致的线程模型。
- 动画等逐帧内容继续正常变化，但每一帧看到的是提交时完整快照，不会与下一帧写入形成数据竞争。
- UI、Debug Draw、动态 Transform 和骨骼姿态需要复制或使用完成回执保护的帧槽，增加一定 CPU 内存
  与同步成本。
- Granit 类型仍封装在后端实现中，公共 C ABI、RID 和 Service 分层不因线程实现而改变。
- 队列、完成回执、过载、恢复与关闭成为必须测试的正式生命周期，而非示例级辅助代码。

## 替代方案

- **继续全部在主线程渲染**：实现简单，但上传与 GPU 等待会持续影响窗口响应。
- **直接复用 Granit Model Viewer 执行器**：短期代码少，但依赖示例私有接口并混入特定 Viewer 状态。
- **由多个线程直接调用同一 Renderer**：省去任务复制，但所有权不清晰且容易产生数据竞争。
- **无限命令队列**：避免生产者重试，却会在过载时持续增加内存和输入延迟。
- **首版采用无锁队列**：可能降低同步开销，但在没有瓶颈数据前显著增加正确性与关闭复杂度。
