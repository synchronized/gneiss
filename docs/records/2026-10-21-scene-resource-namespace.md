<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Scene/Prefab 资源描述命名空间迁移

日期：2026-10-21。依据 ADR-069 与模块布局计划。

## 交付

Scene/Prefab 作者描述、诊断、属性覆盖值、解析/序列化函数、Prefab Loader 与描述租约
从 scene_internal 迁入 gneiss::engine::resource。原路径已经位于 resource，本批统一对象归属。
功能层实例化、活动场景节点与编辑操作继续保留原职责，调用资源描述时显式限定资源命名空间。
没有旧别名，没有改变格式、缓存、引用计数、取消及生命周期语义。

资源目录不再声明或引用 scene_internal，边界检查增加相应正反例。
公共 C ABI 与共享值类型未改变；内部 C++ 使用者须随源码一起重编译。

## 验证

- Windows Clang Debug 完整构建通过。
- 29 项定向回归通过，涵盖加载、实例化、作者描述、属性覆盖、编辑、保存和历史。
- 3 项边界/API 检查通过，包含命名空间回流负向夹具。
- 无线程静态 CAORS Application 构建及测试通过。
- 共享/静态安装包的 8 项 consumer 重链接与运行通过。
- clang-format 与 git diff --check 通过。

日志位于本地 build/scene-resource-namespace-*.log。未运行图形窗口、跨平台或远端 Actions。

## 剩余工作

功能层 scene_internal/world_internal/render_internal/input_internal 及 core/platform 的
命名统一仍待继续；本批不声称整个 Engine 已迁移完成。
