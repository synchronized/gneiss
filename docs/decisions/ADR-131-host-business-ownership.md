<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-131：宿主会话与业务状态归属

## 状态

2026-11-22：接受设计方向，待实施。详细接口、迁移与验收由
[PLAN-128](../plans/PLAN-128-host-business-ownership.md) 维护。

## 背景

Application 调度与 World 系统已建立，但 Editor 入口仍承担大量业务状态，
部分静态库混合文档、视口和 IPC 业务。继续向 Application 或 document_editing
集中状态会模糊服务、编辑内容和运行投影的生命周期。

## 决策

- Application 继续拥有服务与调度；宿主业务会话拥有 World 及渲染绑定。
- Editor 会话协调文档编辑、投影、加载和视口的共同生命周期；document_editing 保持编辑职责。
- Scene View 的相机、目标和交互状态由视口对象统一拥有，UI 负责绘制与输入采集。
- Runtime 会话拥有游戏运行业务；IPC 只转换请求与结果，不拥有 World。
- 项目工作区拥有项目级配置和后台工作；文档、Runtime 连接与工作区生命周期分别明确。
- 沿用独立 Editor/Runtime 进程，以及关闭旧项目会话后重新进入项目选择窗口的流程。
- 不引入第二套调度器、服务定位器、通用 Context 或强制 system 基类。

## 替代方案与影响

继续扩大 application.cpp 改动最小，但业务所有权仍分散。
让 document_editing 拥有 World、Render 和窗口状态会污染文档编辑边界，故不采用。
按每个目录创建库或为每个系统创建类没有独立职责依据，故不作为目标。

会话与视口增加明确的生命周期协调对象，但必须删除原有重复状态及旧入口，不能只增加转发层。
需要同步迁移调用方、测试和构建依赖；不改变资源所有权、文档权威性或现有通信协议。

## 关联

延续 [ADR-129](ADR-129-application-scheduling.md) 与
[ADR-130](ADR-130-world-system-scheduling.md)，不取代其调度和 World 所有权决策。
