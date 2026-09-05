<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-170：Frame Slot 资源池

## 结果

线程执行器现在把已执行或被替换的 Frame Packet 随完成回执归还。Granit Render Service 维护最多
三个回收槽位；Application 在构造下一帧前先消费完成回执并取得一个空闲槽位。Packet 只有在回执
到达后才重新进入捕获流程，因此生产者不会改写渲染线程仍在消费的数据。

捕获流程会清除旧资源条目和度量值，再写入当前帧；UI、Debug Draw 与哈希容器能够复用已有容量。
Scene Snapshot 本身通过移动转交，不额外复制实例数组。关闭流程先排空并停止执行器，再释放回收槽。

## 验证

- `gneiss.render_executor` 验证执行帧与被替换帧都归还对应 Packet。
- `gneiss.render_frame_packet` 验证回收 Packet 能重新捕获为完整自有数据。
- Windows Clang Debug 相关目标构建和测试通过。
- `git diff --check` 通过。

## 边界

- 当前槽池复用 CPU 容器，不改变 Granit 自身的 GPU 帧槽。
- 稳定资源正文仍会写入每个 Packet；M-171 将以 Render Scene 镜像移除这部分重复复制。
- 槽池容量固定为三个，后续由性能基线决定是否需要配置化。
