<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 从 0.50 迁移到 0.51 开发版

本指南描述当前开发分支的源码变化，不表示 0.51 已发布。

## 外部 C++ 调用

公共 `gneiss::sdk` 对象包装与完整 Application、World、Scene、Render、Input、Reflection C API
均已删除，不提供兼容转发。公共 `.hpp/.inl` 已撤下安装入口，共享值迁入 `src`。
同源码宿主使用内部原生接口。动态 Game Module 也已撤销，原模块代码不会再被加载。
移除工程 game_module 字段；客户逻辑注册方式待后续设计，不提供新的替代 SDK。

完整范围及调用示例见 [C++ 调用边界](../reference/cpp-sdk.md)。安装请使用干净前缀，
避免旧 SDK 头文件残留。内部原生对象不搬到公共头，也不为替代包装而新增平行对象。

## 同源码宿主

Editor/Runtime 使用内部原生对象与服务接口，内部头不安装。Application 装配运行服务与场景域；
World 管理对象和 ECS，Scene 管理场景成员与 Prefab 来源/覆盖，Document 保存编辑状态并查询实际场景。
资源对象使用强引用，缓存使用弱引用；Render Service 通过 RID 管理后端资源。

编辑历史和会话须在 Application 之前关闭。异步资源操作按请求编号接收和取消，不能消费其他调用方的回执。
普通资源逐项采用，不提供跨资源批次回滚；场景/Prefab 的具体操作边界见
[场景实例参考](../reference/scene-instance.md)。

反射使用 `meta` 原生类型视图、属性与容器接口；CPU 数据加载与渲染资源准备分工见
[模块布局计划](../plans/PLAN-051-module-layout.md)。旧资产格式和私有结构不保证兼容，应重新导入资产。

架构决策见 [ADR-082](../decisions/ADR-082-remove-dynamic-game-module.md)，构建验证见
[构建指南](building.md)，发布状态以[验收记录](../records/2026-11-01-0.51.0-release-validation.md)为准。
