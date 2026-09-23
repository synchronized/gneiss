<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-042：资产构建归属 Gneiss 工具链，Granit 只承载 GPU 契约

- 状态：已接受
- 日期：2026-09-06

## 背景

发布包需要面向目标平台生成可缓存、可裁剪的运行资产。KTX2、Basis Universal、Mesh 优化和 Shader
变体都涉及源文件、导入设置和工程依赖；若由 Runtime 临时处理会增加启动成本，若直接放入 Granit
则会让渲染后端承担项目资产语义。

## 决策

- Gneiss 工具链拥有资产发现、导入设置、构建图、缓存键、格式解析、转码、依赖和发布裁剪。
- 首版使用工程 `build/.gneiss-cache` 作为内容寻址缓存，按目标平台与架构隔离，并通过临时文件原子提交。
- Runtime 只加载 Gneiss 运行资产，不把源格式解析作为正式发布路径。
- Granit Runtime 提供后端无关的 GPU 资源描述、格式能力查询与上传契约；独立的 AssetTools
  组件可负责通用 GPU 资产 Manifest、摘要和内容 ID 的确定性构建，不感知 Gneiss 工程或资产格式。
- AssetTools 仅链接 Gneiss 离线工具和需要生成测试数据的测试目标，不进入引擎 Runtime 的链接接口。
  图片解码、压缩策略、缓存和 Gneiss 外层封装继续由上层管理。
- KTX2/Basis Universal 依赖保持私有，不通过 Gneiss 公共 C/C++ API 泄漏其类型。
- 发现 Granit 通用能力缺口时，Gneiss 只形成最小接口与验收建议，由用户决定并提交 Granit PR。

## 影响

资产构建可以在 Editor、命令行和自动化之间复用，并按内容稳定缓存；Granit 不被工具格式绑定，其他
上层项目也能复用其 GPU 能力。代价是 Gneiss 需要维护构建处理器和运行资产转换，并验证工具版本变化
能够正确使缓存失效。

## 替代方案

- 由 Granit 直接加载 KTX2：接入较快，但把文件容器和工具链策略固化进渲染层。
- Runtime 首次加载时转码：减少离线步骤，但启动延迟、平台一致性和 Shipping 裁剪难以控制。
- 每种格式独立脚本构建：前期简单，但缓存、依赖、诊断和 Editor 接入会形成多套规则。
