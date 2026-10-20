<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CAORS 共享运行时接入验收

## 范围

Windows、Clang、Debug；CAORS 固定提交 cbe41b75223f52bec746cc23346b28eba90fa693，
通过现有源码缓存或本地覆盖构建为共享库。未修改上游源码。
决策见 [ADR-068](../decisions/ADR-068-caors-resource-probe.md#caors-共享运行时)，
后续资源接入见[实施计划](../plans/PLAN-051-caors-shared-material.md)。

## 结果

- 主开发配置启用输入与材质：Engine、Editor 与相关测试目标构建成功。
- CAORS asset-probe、vfs-bridge、application、material-document，以及 Editor asset-edit、
  隐藏 smoke、texture-load-service 共 7 项 CTest 通过。
- 静态 Engine 和共享 Engine 两组轻量配置均启用 CAORS 材质、关闭 Granit 窗口与 CAORS 输入；
  构建及安装成功，各自的 C、C++、application_world、property_inspector 消费测试共 4 项通过。
- 两组干净安装前缀均包含 caors.dll、CAORS package 和 caors-LICENSE。
- llvm-readobj 导入表确认开发 Engine、独立探针、安装后的共享 Engine 和静态 Engine
  C++ consumer 均引用 caors.dll，验证没有继续使用旧静态链接产物。

本地日志位于 build/caors-shared-tests.log、build/caors-shared-sc-mat-s-consumer.log
及 build/caors-shared-sc-mat-h-consumer.log，不提交构建日志。

## 限制

未验收 Linux、macOS、Emscripten、Release 或可见窗口手动操作，不作为性能改善证明。
上游 Clang 构建报告继承构造函数 dllexport 支持警告；未关闭警告或修改上游。
共享库方式仍要求宿主与 CAORS 使用兼容工具链/运行库，不提供跨编译器 C++ ABI 保证。
本轮完成运行时链接与部署接入，Mesh/纹理资源迁移仍未完成，生产功能开关仍默认关闭。
