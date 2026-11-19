<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-129：Application 调度与宿主状态组合

## 状态

2026-11-19 接受目标架构，尚未实施。实施步骤、API 草案与未决细节以
[PLAN-126](../plans/PLAN-126-application-scheduling.md) 为唯一来源。

## 背景

Application 集中维护基础服务和场景、输入映射、视图等业务状态；不同宿主依靠大量回调与专用
转发接口共享循环。固定 update 回调难以表达独立模块的进入、退出与运行条件；单一状态映射到
完整系统列表又会使加载和暂停误停必要服务。

## 决策

Application 管理应用级服务、顺序调度与启动/停止；宿主 Session 管理 World、渲染绑定和业务请求。
引擎生命周期与宿主状态分离。基础事件/完成分发不受业务状态限制，业务系统按状态选择，转换在
主线程明确边界延迟提交。system 为可调用项，安装函数负责组合，不要求 ECS 存储或 system 基类。
World 到 Render 的命令同步仍由 World 侧绑定承担，不下沉到 Application 或 Render 后端。

启动等待与关闭排空是生命周期协议，不依赖每帧重复初始化或简单反序 shutdown。借用捕获在停止
完成前必须有效；Application 不因异步回调而强制共享所有权。run 与外部 step 共用执行路径。

## 取舍与替代方案

相比固定 client 回调，该方案允许 Editor、Runtime、项目管理器组合流程，代价是必须规定状态
冲突、系统执行顺序、异步取消与关闭契约。相比完整 ECS 调度框架，不引入自动参数注入、读写冲突
推导和并行执行；需要并行的任务继续通过现有调度器提交。
不采用每状态一套完整循环，因为事件和完成队列需要持续运行；不为每个功能引入子 Application。
不采用通用服务注册器替代资源所有权，服务依赖及失败清理由真实装配契约保证。

## 参考

- [Bevy 插件组合](https://bevy.org/learn/quick-start/getting-started/plugins/)
- [Bevy 状态示例](https://bevy.org/examples/games/game-menu/)
- [Godot MainLoop](https://docs.godotengine.org/en/stable/classes/class_mainloop.html)
- [Unreal Subsystems](https://dev.epicgames.com/documentation/unreal-engine/programming-subsystems-in-unreal-engine)

参考用于比较组合与生命周期思路，不承诺复制上述引擎的实现或调度语义。
