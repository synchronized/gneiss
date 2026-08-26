<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Gneiss 文档中心

这里是 Gneiss 使用指南、接口参考、架构说明和开发计划的统一入口。根 README 只负责项目介绍与最小入口；具体事实以本页链接的对应文档为准。

## 推荐阅读顺序

### 了解项目

1. 阅读项目根目录的 [README](../README.md)，了解定位与当前状态。
2. 阅读[项目目标与边界](concepts/project-scope.md)，了解 Gneiss 解决的问题和与 Granit 的分工。
3. 阅读[总体架构](concepts/architecture.md)，了解场景树、ECS、服务与后端的关系。
4. 阅读[开发路线图](roadmap.md)，了解阶段、优先级和当前工作重点。

### 参与开发

1. 阅读[项目文档规范](../DOCUMENTATION_GUIDE.md)。
2. 阅读 [C/C++ 代码风格与语言标准](guides/coding-style.md)。
3. 通过 [GitHub Issues](https://github.com/synchronized/gneiss/issues) 确认任务范围。

## 操作指南

- [构建、测试与运行 version 示例](guides/building.md)
- [C/C++ 代码风格与语言标准](guides/coding-style.md)

## API 参考

- [Core 版本与结果接口](reference/core.md)
- [RID 有效性与 Service 生命周期](reference/rid.md)

## 架构与原理

- [项目目标、核心边界与非目标](concepts/project-scope.md)
- [总体架构、模块边界与核心技术](concepts/architecture.md)

## 路线图与开发计划

- [Gneiss 开发路线图](roadmap.md)
- [M-01：0.1.0 最小运行时闭环](plans/M-01-0.1.0-runtime-slice.md)

## 架构决策

- [架构决策索引](decisions/README.md)
- [ADR-001：Granit 依赖接入边界](decisions/ADR-001-granit-dependency.md)

## 文档维护

- [项目文档规范](../DOCUMENTATION_GUIDE.md)

其他教程、API 参考、ADR 和实施记录将在出现真实内容后加入本页。尚未实现的设计不是当前公共能力。
