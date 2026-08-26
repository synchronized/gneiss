# Gneiss

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++](https://img.shields.io/badge/Standard-C++20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey.svg)](https://github.com/synchronized/gneiss)

**Gneiss** 是一款基于 [Granit](https://github.com/synchronized/granit) 构建的现代 C++20 游戏引擎。项目融合 ECS 的高性能数据驱动架构与服务导向的资源管理方式，致力于提供模块化、高性能且对开发者友好的游戏开发环境。

> [!WARNING]
> 项目目前处于早期活跃开发阶段，API 尚未稳定，不建议直接用于生产环境。

## 设计目标

- **模块化架构**：各系统以独立服务的形式提供，并通过轻量级资源句柄（RID）交互。
- **高性能数据驱动**：以 ECS 作为核心数据层，改善数据局部性并提高 CPU 缓存利用率。
- **分层数据管理**：采用场景树与 ECS 双栈架构，兼顾开发便利性和运行效率。
- **异步网络**：基于 `libuv` 实现网络能力，通过无锁队列与主线程解耦。

## 架构概览

Gneiss 分为逻辑层、ECS 数据层、服务层与后端层。场景树通过实体 ID 映射到 ECS，运行时系统再通过 RID 使用渲染、网络、音频和物理服务。

完整的分层关系、模块边界和技术选型见[总体架构](docs/concepts/architecture.md)。

## 开始使用

项目仍在搭建中，构建依赖、编译步骤和最小示例将在首个可运行版本发布后补充。

你可以通过 [Issues](https://github.com/synchronized/gneiss/issues) 提交建议或跟踪开发进展。

## 文档

使用指南、接口参考、架构说明和开发计划统一收录在 [Gneiss 文档中心](docs/README.md)。

文档的职责、目录、模板和维护规则见[项目文档规范](DOCUMENTATION_GUIDE.md)。

## 参与贡献

欢迎参与 Gneiss 的开发。在提交代码前，建议先创建 Issue 描述使用场景、目标与初步方案，以便共同确认实现范围。

## 许可证

本项目基于 [MIT License](LICENSE) 开源。
