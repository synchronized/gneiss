<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 从 0.50 迁移到 0.51 开发版

本指南描述当前开发分支的源码变化，不表示 0.51 已发布。

## 外部 C++ 调用

公共 `gneiss::sdk` 对象包装与完整 Application、World、Scene、Render、Input、Reflection C API
均已删除，不提供兼容转发。公共 `.hpp/.inl` 已撤下安装入口，共享值迁入 `src`。
同源码宿主使用内部原生接口；外部游戏模块使用最小 Game Module 协议，原完整 SDK 消费者需迁移。
安装头只保留模块协议及必需类型，引擎构建版本头不再安装。

完整范围及调用示例见 [C++ 调用边界](../reference/cpp-sdk.md)。安装请使用干净前缀，
避免旧 SDK 头文件残留。内部原生对象不搬到公共头，也不为替代包装而新增平行对象。

## 同源码宿主

Editor/Runtime 使用内部原生对象与服务接口。外部游戏模块使用安装协议，不能包含 `src/`
私有头。原生 Application 拥有运行服务和场景域，场景域拥有 World 与场景实例；Scene/Prefab
只借用所属 World，共享资源由原有资产服务管理。

Editor 会话、资产事务及历史命令须在宿主 Application 之前关闭。场景切换后不可继续借用旧域
服务；会话通过活动域身份校验，候选取消和回收继续由既有事务流程推进。
动态游戏模块的真实 ABI 边界保留；仓内反射调用使用原生接口。

架构决策见 [ADR-080](../decisions/ADR-080-minimal-game-module-api.md)，构建与安装验证见
[构建指南](building.md)。当前迁移进度以 [DEV-051](../plans/DEV-051-architecture-boundaries.md) 为准。
