<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-106：Runtime 基础运行统计实施记录

## 结果

M-106 已建立低频运行统计闭环。Runtime 每 250 ms 在主线程安全点生成统计快照，经既有本机 IPC
连接发送；Editor 只接受当前 Runtime 检查会话中序号更新的快照，并在 Runtime 层级区域显示：

- 最近一帧耗时及据此换算的瞬时 FPS；
- 累计固定更新次数；
- 场景节点数与 World 实体数；
- IPC Transport 当前待写数量与累计丢弃事件数。

## 边界

统计消息使用独立单调序号并复用 Runtime 检查会话 ID，旧会话数据不会覆盖新会话。数据仅用于即时
诊断，不持久化、不聚合历史曲线，也不构成完整 Profiler。IPC 水位来自线程安全的原子计数，I/O
线程不访问 Scene Tree 或 ECS；节点和实体数量仍由 Runtime 主线程查询。

统计属于低优先级流量。发送队列暂时满时允许丢弃当前统计快照，不因此终止 Runtime；控制消息的
优先级隔离、显式重同步和过载预算由 M-107 完成。

## 验证

协议测试覆盖统计字段完整往返；真实 Editor–Runtime 进程测试验证同一会话能够收到非空节点与实体
统计。Windows Clang 严格构建与相关测试已通过。
