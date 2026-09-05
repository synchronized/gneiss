<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-173：渲染背压与帧节流

## 结果

Application 在构造 Scene Snapshot 和捕获 Frame Packet 之前先消费渲染完成回执，并检查待处理 Frame
数量。队列达到三个待处理 Frame 时，本轮不再构造必然造成额外积压的渲染数据；窗口事件、输入、
游戏逻辑和下一轮循环保持运行。

跳过构造次数进入线程安全统计快照，并包含在关闭诊断摘要中。资源及控制 Command 和必须完成 Frame
保持显式过载结果，不与普通实时帧共享静默替换策略。

## 验证

- 执行器测试覆盖跳过构造计数的线程安全读取。
- Engine 与执行器目标在 Windows Clang Debug、警告视为错误条件下构建通过。
- `git diff --check` 通过。

## 边界

- 首版依据待处理 Frame 深度节流，不暂停 Scene/ECS 或游戏逻辑时间。
- 正在执行的 Frame 不计入队列深度；GPU 帧槽仍由 Granit 自身约束。
- 更细粒度的目标帧率和 Present 节奏控制不属于本里程碑。
