<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 从 0.50 迁移到 0.51 开发版

本指南描述当前开发分支的源码变化，不表示 0.51 已发布。

## 外部 C++ 调用

公共 `gneiss::sdk` 对象包装已删除，不提供兼容转发。C ABI 函数、结构布局和动态模块协议保持不变。
删除 `gneiss/gneiss.hpp`、`gneiss/engine/application.hpp` 与 `gneiss/engine/game_module.hpp`，
调用方改用对应 `.h` 的创建、操作及销毁接口；销毁前清理依赖该对象的资源，并处理返回码。
其余公共 `.hpp/.inl` 也已撤下安装入口；共享值迁入 `src`，不再作为安装 SDK 提供。
完整 C API 仍处于撤销迁移中，新游戏模块使用最小 Game Module 协议。

完整范围及调用示例见 [C++ 调用边界](../reference/cpp-sdk.md)。安装请使用干净前缀，
避免旧 SDK 头文件残留。内部原生对象不搬到公共头，也不为替代包装而新增平行对象。

## 同源码宿主

Editor/Runtime 使用内部原生对象与服务接口。外部消费者继续使用安装 SDK，不能包含 `src/`
私有头。原生 Application 拥有运行服务和场景域，场景域拥有 World 与场景实例；Scene/Prefab
只借用所属 World，共享资源由原有资产服务管理。

Editor 会话、资产事务及历史命令须在宿主 Application 之前关闭。场景切换后不可继续借用旧域
服务；会话通过活动域身份校验，候选取消和回收继续由既有事务流程推进。
动态游戏模块和反射回调的真实 ABI 边界保留，不应按普通内部调用机械删除。

架构决策见 [ADR-062](../decisions/ADR-062-internal-cpp-and-sdk-boundaries.md)，构建与安装验证见
[构建指南](building.md)。当前迁移进度以 [DEV-051](../plans/DEV-051-architecture-boundaries.md) 为准。
