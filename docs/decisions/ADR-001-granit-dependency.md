<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-001：Granit 依赖接入边界

- 状态：已接受
- 日期：2026-08-26

## 背景

Gneiss 需要复用 Granit 的渲染、窗口和输入能力。开发环境可能将两个项目放在同一父工程中，也可能
只提供已安装的 Granit CMake package。若 Gneiss 直接依赖 Granit 源码目录或传播其全部目标，下游
构建、安装和模块边界会与 Granit 的仓库布局耦合。

## 决策

- `0.1.0` 最低支持 Granit `0.3.0`。
- Gneiss 内部只通过 Granit 导出的命名空间目标链接，首个核心目标使用 `granit::granit`；窗口、
  输入和高层组件按实际任务显式增加。
- 若父工程已经提供所需 Granit 目标，Gneiss 直接复用；否则通过
  `find_package(granit 0.3 CONFIG REQUIRED)` 查找已安装 package。
- Gneiss 不在普通配置中隐式下载 Granit，也不硬编码相邻仓库路径。开发者可以通过安装前缀或父
  工程选择源码版本，CI 必须使用明确锁定的版本。
- Granit 链接保持为 Gneiss 实现细节，不向 Gneiss 普通公共目标传播 Granit 头文件、宏或链接要求。
- Granit 错误在 Service 边界转换为 Gneiss 结果码，并通过诊断接口保留详细上下文。

## 影响

- Gneiss 可以独立构建，也能作为更大父工程的一部分复用已存在的 Granit 目标。
- 源码联调需要父工程或开发者自己的 `CMakeUserPresets.json`，仓库 preset 不依赖本机目录结构。
- Render、Window 和 Input Service 落地时需要增加统一依赖解析函数，并测试父目标与安装 package
  两条路径。
- Granit 类型不能直接成为 Gneiss ECS 组件或普通公共 ABI 的一部分。

## 替代方案

- **Git submodule 或 vendoring**：版本明确，但会复制大型第三方源码和其依赖，首版不采用。
- **FetchContent 自动下载**：配置方便，但会产生隐式网络和版本解析行为，首版不采用。
- **硬编码相邻仓库**：只适合单台开发机，不可复现，拒绝采用。
