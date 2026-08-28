<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-68：安装、升级与依赖消费验收记录

## 结论

M-68 已完成。Gneiss 的核心 SDK 与 Granit 运行时均可通过 Shared/Static 安装产物被独立工程消费；
Granit 的 `PACKAGE` 与 `FETCH` 来源路径、0.9.0 Application v1 冻结布局以及 Windows/Linux 手动矩阵
均已通过验证。

## 主要改动

- 建立与源码树隔离的纯 C、C++20 和稳定运行时安装 Consumer。
- 导出安装后的运行库目录，并为 Shared Consumer 显式组装 Gneiss 与 Granit 动态库搜索路径。
- 在 `FETCH` 模式记录 Granit 的实际构建目录，使其安装产物可进入独立验收前缀。
- 增加冻结的 0.9.0 Application v1 Consumer，验证旧布局可调用当前 1.0 候选库。
- 增加 0.9.0 到 1.0.0 升级指南，说明稳定边界、重编译要求与运行时动态库部署方式。
- 修正 Windows 无 Vulkan ICD 环境的图形测试筛选；真实稳定运行时 smoke 由 Linux Granit 矩阵覆盖。

## 最终验证

- [Linux 手动矩阵](https://github.com/synchronized/gneiss/actions/runs/33189914525)：Clang/GCC、
  Shared/Static 核心构建以及 Granit Shared/Static 运行时，共 6 项全部通过。
- [Windows 手动矩阵](https://github.com/synchronized/gneiss/actions/runs/33189903680)：MSVC Shared/Static
  运行时及安装后 Consumer，共 4 项全部通过。
- API 稳定性清单校验通过：83 个导出符号与基线一致。
- 本地隔离验证覆盖核心 Shared/Static Consumer、Granit `PACKAGE`/`FETCH`、已安装稳定运行时样例及
  0.9.0 Application v1 ABI Consumer。

## 边界与后续

Windows 托管 Runner 没有 Vulkan ICD，因此不在该环境启动真实图形窗口；这不降低构建、链接和安装
消费检查，图形 smoke 由 Linux Granit Shared/Static 任务负责。M-69 将继续建立性能、内存与故障
诊断基线，M-70 再对冻结的 1.0.0 发布候选执行发布包验收。
