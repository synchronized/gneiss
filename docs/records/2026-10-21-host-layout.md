<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 独立 Host 模块迁移验收

日期：2026-10-21。按用户确认的方案 B 和
[ADR-069](../decisions/ADR-069-resource-runtime-boundaries.md) 实施。

## 已完成

- apps/common、apps/editor、apps/runtime 的实现与宿主测试迁入 src/host 对应模块。
- assetc 命令行实现迁入 src/host/assetc；Apps 的 Editor、Runtime、project、assetc
  main 仅转发宿主入口。没有保留旧路径转发头。
- Runtime 宿主使用 gneiss::host::runtime，避免与原生 gneiss::runtime 业务对象混淆。
- 更新构建、示例工作流测试和测试支持头路径；目标名、安装程序名及命令行保持不变。
- 依赖检查拒绝 Engine、Runtime、Editor 反向包含 Host，以及 Host 反向包含 Apps；
  负例覆盖各层，正例允许 Host 装配 Editor 和 Runtime。

## 验证

Windows Clang Debug 全量构建通过。70 项宿主、协议、编辑器、资产命令行和边界测试通过；
另外 Runtime 安装冒烟、Runtime 与 Editor 启动诊断 3 项通过，共 73 项。
安装测试覆盖安装后的 Runtime、项目打包/校验/导出及游戏模块构建运行。
格式检查与 git diff --check 通过。

## 限制与后续

未执行 POSIX/Web 构建、远端 Actions 或 GUI 人工验收。宿主测试暂随对应模块存放。
Editor 宿主还含混合业务/UI 装配，后续继续拆入 src/editor，并保持 Host 单向依赖。
CPU 缓存和其他对象职责收敛未因目录迁移而完成，剩余项以
[布局计划](../plans/PLAN-051-module-layout.md)为准。
