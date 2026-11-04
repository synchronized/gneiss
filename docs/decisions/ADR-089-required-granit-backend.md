<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-089：固定 Granit 后端依赖

- 状态：已接受。
- 日期：2026-10-27。

## 背景

Editor 与 Runtime 宿主已经依赖 Granit，原有可选平台构建却仍维护无渲染替身、
应用条件分支和纹理解码限制，增加构建组合与行为差异。

## 决策

Granit 成为 Engine 的固定构建依赖，删除平台开关、对应预处理宏及无渲染替身。
窗口与 GPU 的初始化仍由 Application 的 enable_window 控制；纯逻辑运行不初始化两者。
链接后端不等于启动后端。暂不接入 Granit headless，也不新增渲染模式接口。

## 影响

所有 Engine 构建都需要解析 Granit 的 Window 与 RenderPipeline 依赖；静态消费者也需要这些依赖。
纯逻辑测试继续使用真实服务的未初始化状态。CPU 纹理解码不再受窗口后端开关限制。
代价是不能通过关闭平台选项减少编译依赖；收益是消除重复实现与不一致的功能组合。
当前使用方式见 [Application 参考](../reference/application.md)。
