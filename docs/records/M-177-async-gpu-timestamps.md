<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-177：异步 GPU 时间戳

## 结果

Granit Render Service 现在为每个被执行器接受的 Frame Packet 分配稳定序列号，并在渲染线程内按
三个 Frame Slot 使用六个 Timestamp Query。每帧在 GPU 工作顶部与底部写入时间戳；同一 Frame Slot
下一次被 Frame Context 安全复用时才读取旧结果，因此不会等待当前帧 GPU 完成。

读取结果携带原始 Frame 序列，经过现有完成回执进入线程安全统计快照。退出诊断新增计时能力、有效
样本、暂不可用样本以及最近 GPU 帧序列和耗时。

## 失败与生命周期

- 后端明确不支持 Timestamp Query 时，初始化继续成功并将 GPU 计时报告为不可用。
- 结果暂不可读时丢弃该次诊断样本，不阻塞或判定渲染失败。
- Query 命令或读取发生非暂时错误时关闭后续 GPU 计时，正常渲染路径继续运行。
- 只有成功提交且完整写入起止时间戳的槽位才标记为待读取；失败、取消和未提交帧不会发布样本。
- Timestamp Query Pool 在 Renderer 资源统计前由渲染线程销毁，Resize 与关闭不跨线程访问查询对象。

## 验证

- 执行器测试覆盖自动 Frame 序列、延迟样本关联、有效/暂不可用计数和最近 GPU 时间快照。
- Windows Clang Shared Debug：完整构建及 131/131 测试通过。
- Windows Clang Static Debug：完整构建及 129/129 测试通过。
- Runtime、Editor 与 Granit 平台冒烟通过。

## 已知边界

- 当前只记录整帧 GPU 顶部到底部区间，不提供逐 Pass、Pipeline Statistics 或历史曲线。
- 无真实 GPU 的单元测试只验证序列与统计传播；后端时间戳数值由平台冒烟和后续跨平台矩阵验证。
