<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-294：World C++ 内部契约与 C 入口拆分

日期：2026-10-05。0.45 实施中的一个子阶段，不代表 M-294 或版本整体完成。

## 改动与边界

- 27 个 World、节点与内建反射 C 入口移入 `src/api/c/world_api.cpp`，保留既有符号和布局。
- `src/world/world_service.hpp/.cpp` 提供内部引用参数接口；唯一 World 注册表继续管理 generation、
  domain、创建线程及 ECS/Scene Tree 寿命。没有复制注册表，也没有导出 World 状态或 EnTT 类型。
- 相机内部使用三个投影参数组成的 `camera_settings`，结构大小与保留字段校验留在 C 适配。
  普通 Transform、Entity ID 等固定布局值继续复用，不机械创建第二套值结构。
- Application 场景装配、Scene/Prefab 实例及组件属性访问直接调用内部 World 接口。
  创建/销毁、失效句柄、线程错误、相机有效性及失败输出规则保持原有语义。
- World 的实现头迁为 `.hpp`；Core 边界检查扩展到 World，并增加禁止内部绕回 World C ABI 的反例。

这些内部函数为本仓库同版本宿主与实现测试提供私有 C++ 导出；头文件不安装，不承诺跨版本 C++ ABI。
公共使用者仍按 C ABI 或外层 RAII SDK 访问，不应依赖这些导出。

## 验证

Windows Clang Debug 共享库构建通过；迁移后完整 CTest 168/168 通过（192.60 秒），包括
Editor/Runtime 工作流、Prefab 事务刷新、Cook、渲染快照与 GPU 像素测试。此次没有启用 Vulkan
validation layer，因此不能据此宣称 validation 检查通过。

新增 C/私有 C++ 混合访问测试验证同一注册表、旧 generation 失效、跨线程销毁拒绝、无效相机
不覆盖原组件，以及错误优先级和失败查询输出。共享新增与相关专项 10/10 通过（7.88 秒）；静态库全量构建通过，相关 19/19 专项通过（10.34 秒），包含安装消费者。

静态检查完成；保留固定宽度句柄的相邻参数告警及既有初始化列表尾逗号建议，没有关闭检查级别。
此次没有将内部 World ID 另建一套拥有型包装。

## 尚未完成

组件反射注册仍使用 Reflection 的 C 入口，随 Reflection 自身迁移收口。
Application 注册表及其混合入口、Scene 服务的公开描述转换、其余模块目录与完整 SDK 清单仍待完成。
全平台矩阵、真实大场景回归和发布验收尚未执行；不以本记录替代完整版本门槛。
