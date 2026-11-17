<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Files 文件布局与初始化迁移验收

## 范围

2026-11-17 完成 files 命名空间下 service、types、backend、read_stream 文件名收敛，
路径校验移入 src/detail。四种后端由 backends 下配置和工厂创建，具体类隐藏在 cpp。
Native 移除仅用于隐藏类成员的 PImpl，Browser 保留异步保活所需共享实现。

服务负责去重初始化并通过现有完成队列通知，冻结挂载、限制初始化前 I/O、汇总失败与等待关闭。
Application 使用弱引用接收完成通知，完成后启动环境资产加载；迟到通知不会访问已销毁宿主。
测试和各模块 include、CMake 源文件列表、Fetch JS 引用同步迁移。无旧路径转发头。

接口约定以 [文件服务参考](../reference/file-service.md) 为准；决策见
[ADR-123](../decisions/ADR-123-files-backend-initialization.md)。

## 验证

- Windows Clang Debug 完整共享构建通过。
- 最终 17 项相关测试通过：Files service/native、资产 application/原生窗口/manager/material、
  安装 Runtime smoke、core-boundary、场景加载/准备、render_asset_loader、texture-load-service、
  原生窗口 smoke、action_map、原生应用流程、作者资产服务和材质编辑器。
- 文件测试覆盖去重、跨线程完成、初始化前拒绝 I/O、延迟用户通知、拒绝与异步失败汇总、
  初始化期间关闭、通知预算、空挂载及析构后迟到完成；原有读写、回退和分块测试保留。
- 静态 Engine 与 Files service/native 目标构建通过，2 项对应测试通过。
- 边界检查首轮因迁移后的 JS 桥接白名单路径不匹配失败，修正后通过；负向边界用例也通过。
- 作者资产服务首轮在构建期间超时；构建结束后原用例通过，没有放宽时限或跳过测试。

## 限制

本机没有 Emscripten，Web 仅完成源码与构建引用迁移，未验证编译或浏览器运行。
未运行完整静态测试集、Linux 或远端 Actions；本轮未额外验证静态安装消费。
本地原始日志位于 build/files-refactor-*.log，不作为发布产物。
