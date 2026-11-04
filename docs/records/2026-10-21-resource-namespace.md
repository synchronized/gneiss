<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 资源模块命名空间首批统一

日期：2026-10-21。依据 ADR-069 与[模块布局计划](../plans/PLAN-051-module-layout.md)。

## 交付

asset_internal 统一为 gneiss::engine::resource，asset_manager、action_map_request 及其
CAORS 请求工厂同步从 engine 顶层移入资源领域。更新 Runtime、Render、Scene、Input、
离线工具和测试调用方。旧命名空间和对象别名不保留。
边界检查新增旧资产命名及管理器引用的负向夹具。

本批仅调整命名与归属表达，不改变缓存策略、线程调度、对象寿命、取消及错误行为，
不改变公共 C ABI 或共享值类型。内部 C++ 接口须与库同源码重编译。

## 验证

- Windows Clang Debug 完整构建通过。
- 37 项资源/场景/编辑定向回归及 3 项边界/API 检查全部通过。
- 无线程静态 CAORS Application 构建与测试通过。
- 更新共享/静态安装包后，8 项 C/C++/示例 consumer 重链接及测试全部通过。
- clang-format 与 git diff --check 通过。

原始日志位于本地 build/resource-namespace-*.log。

## 剩余范围

资源目录中的 Scene/Prefab 作者描述和 Prefab Loader 仍使用 scene_internal，须与功能层
Scene 的交叉引用逐项迁移，不能将运行时 Scene 整体替换到资源层。
CAORS 平台适配仍使用其原有命名空间；Engine function/core/platform 后续按计划迁移。
未运行图形窗口、Linux/macOS/Emscripten 或远端 Actions，不代表整个命名统一任务完成。
