<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-003：Granit 依赖来源策略

- 状态：已接受
- 日期：2026-08-26
- 取代：[ADR-001](ADR-001-granit-dependency.md)

## 背景

Gneiss 既需要在 Granit/Gneiss 联合父工程中复用源码目标，也需要消费已安装 package。只允许这两种
方式会让首次构建必须预先安装 Granit，不符合开箱构建目标；无条件下载又会破坏离线、发行和严格
CI 环境的可控性。

## 决策

- Granit 最低版本保持为 `0.3.0`，普通接口仍不得泄漏 Granit 类型。
- `GNEISS_GRANIT_PROVIDER` 支持 `AUTO`、`PACKAGE` 和 `FETCH`，默认使用 `AUTO`。
- 所有模式首先复用父工程已经存在的 `granit::window` 等所需目标。
- `AUTO` 依次尝试父工程目标、已安装 package，找不到时通过 FetchContent 下载锁定提交。
- `PACKAGE` 禁止下载；package 或所需组件缺失时在配置阶段失败。
- `FETCH` 跳过 package 查找并下载 `GNEISS_GRANIT_GIT_REPOSITORY` 的
  `GNEISS_GRANIT_GIT_TAG`。默认值使用 HTTPS 仓库和完整提交哈希，不跟踪浮动分支。
- Fetch 模式关闭 Granit 自身测试、示例、benchmark、工具及可选集成，只构建 Gneiss 链接到的目标。
- FetchContent 使用各构建目录下的 `_deps` 源码和构建目录，不写入仓库 `3rd/`。
- 发行构建和不允许网络的 CI 使用 `PACKAGE`；CI 另设干净环境验证 `FETCH` 路径。

## 影响

- 普通开发者启用 Granit 能力后无需手动安装依赖。
- 首次 Fetch 配置需要网络且耗时更长，后续复用构建目录缓存。
- 依赖更新必须先验证 Granit，再显式更新完整提交哈希和本 ADR 的后继记录。
- 企业镜像或源码联调可以覆盖仓库 URL、提交，或直接由父工程提供目标。
- `3rd/` 继续只保存随 Gneiss 仓库锁定的内部依赖，不复制 Granit 仓库。

## 替代方案

- **只允许 package/父工程**：最可控，但首次构建门槛较高，不再采用。
- **无条件 FetchContent**：开箱简单，但无法满足离线和发行构建，拒绝采用。
- **Granit Git submodule**：版本直观，但扩大仓库和子模块维护成本，暂不采用。
