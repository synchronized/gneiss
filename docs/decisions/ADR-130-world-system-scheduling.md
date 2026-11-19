<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-130：World 提供系统注册与更新入口

## 状态

2026-11-20 接受接口方向，尚未实施。详细接口、实施规则与验收以
[PLAN-127](../plans/PLAN-127-world-system-scheduling.md) 为唯一来源。

## 背景

不同运行空间需要不同业务系统，例如游戏需要模拟，预览只需要动画。
只在 Application 中注册捕获不同 World 的系统，列表及运行状态的生命周期容易分散。
此前讨论的公开 world_session/world_schedule 组合要求调用者手动访问内部成员并
systems.run(world, time)，没有提供完整的 World 使用入口；该讨论方案未实施。

## 决策

World 提供 add_system 与 update，拥有本运行空间的系统 callable，串行按注册顺序运行，
首次更新前完成注册。系统显式接收 world& 和帧时间，无状态实现可复用，
有状态实例按 World 分别拥有。World 不因增加调度而持有 Application 或共享服务。

Application 继续承担应用阶段、共享服务和生命周期；宿主持有 World，
决定是否及何时更新，并负责切换与关闭。World 系统不自动推进文件、资产或窗口。
渲染绑定与帧提交保持应用侧明确阶段，World 更新不隐式触发渲染。
多文档不固定映射为多个 World；由编辑会话决定隔离或联合投影。

此决策扩展 [ADR-129](ADR-129-application-scheduling.md) 的业务调度边界，
不替代其 Application 生命周期、系统失败或服务关闭协议。

## 取舍与替代方案

- 相比公开 session.world/session.systems，World 入口减少调用方拼装，系统列表与数据共同销毁。
  代价是 World 增加调度职责；范围限定为本 World 的业务更新。
- 相比全部注册到 Application，独立 World 的更新、暂停和系统状态更容易隔离；
  宿主仍需明确更新顺序和共享依赖。
- 不采用完整 ECS 调度图或自动注入：目前没有需要承担其复杂度的真实用例。
- 不强制新增 session 类型。现有宿主状态可继续拥有 World 和渲染绑定，
  只有出现独立职责时才提取对象。
