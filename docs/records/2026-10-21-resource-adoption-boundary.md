<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 资源版本准备与应用修改边界验收

2026-10-21，Windows Clang Debug，0.51 特性分支。
对应[模块布局计划批次 A](../plans/PLAN-051-module-layout.md)。

## 实际改动

- 删除 asset_manager::accept(completion, callback)。管理器只提供 prepare_adoption，
  不再执行场景修改回调；仍使用既有 prepared_adoption，不新增协调对象。
- scene_session::accept_edit_assets 执行业务回调，成功后提交版本选择；失败或异常时
  RAII 释放准备状态。自动资源重载复用该应用层入口。
- 场景候选测试补齐私有 manager 及依赖实现的源码编译。此前构造与析构迁出头文件后，
  该测试仍仅链接 Engine DLL，缺少私有符号；本次不向公共 DLL ABI 新增导出。

## 验证

- 构建 scene-load-builder、scene-load-application、texture-load-application 目标成功。
- 上述三项 CTest 与 core-boundary、core-boundary-negative 共 5 项通过。
- 新增线程池与协作执行路径下的编辑采用测试：空回调、业务失败、bad_alloc、普通异常、
  失败后默认选择不变，以及再次成功提交并改变默认选择。
- 原有场景共享引用更新、来源失败、取消及候选隔离回归通过。
- scene_session.cpp 的 clang-tidy 无诊断，修改的 C++ 文件通过 clang-format 检查。
- git diff --check 通过。

## 限制

本批未迁移物理目录，未统一全部 CAORS 缓存。asset_manager 仍依赖渲染类型与上传通道，
留待计划后续批次处理；低层缓存的旧回调重载也未宣称全部删除。
未运行完整共享/静态消费矩阵、Linux、Web 或图形交互验收；本次未修改公共 ABI。
