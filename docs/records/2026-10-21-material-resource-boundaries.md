<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 材质资源领域边界迁移

日期：2026-10-21。对应 [模块布局计划](../plans/PLAN-051-module-layout.md) 与
[ADR-069](../decisions/ADR-069-resource-runtime-boundaries.md)。

## 完成内容

资源类型、纯字节解码、版本租约/索引与 CAORS 加载适配分开，当前入口见
[目录说明](../concepts/repository-layout.md#cpu-文档归属)。删除原后端目录中的版本索引文件，
不保留兼容转发头。移除 material_document_reference 空虚基类，准备结果和渲染保活字段
改为具体 material_document_lease，测试直接访问只读租约，不使用 dynamic_cast。

新增边界规则拒绝资源类型依赖加载器/缓存/平台，以及 CPU 缓存依赖后端适配；
负例覆盖后端回流，正例允许内部 CPU 资源继承 CAORS resource。

## 验证

- Windows Clang Debug 完整构建通过，警告作为错误。
- 8 项相关测试通过：caors.material-document、scene-load-preparation、prefab-preparation、
  render_asset_preparation、texture-load-service、texture-load-application、scene-load-builder、
  internal-header-visibility。
- core-boundary 与 core-boundary-negative 通过；合计 10 项。
- Emscripten 的 gneiss_engine 静态库构建通过，未运行浏览器界面。
- 材质测试继续覆盖确切版本复用、来源变化/缺失失败不发布、旧版本租约和跨批次保活；
  新增编译期断言确认租约不再依赖虚函数。
- 首次拆分构建发现版本索引隐式依赖后端头提供 result 类型，已改为显式包含共享结果头后构建通过。

本地日志：build/material-boundary-build.log、material-boundary-tests.log、material-boundary-web.log。
未改公共 ABI、安装目标或依赖传播，未重跑安装矩阵；未运行图形窗口冒烟测试。

## 剩余项

同步缓存发布、材质/Mesh 加载统一、CPU/GPU 引用进一步拆分仍未完成。
此次迁移保持运行时行为，不表示整体架构迁移已结束。
