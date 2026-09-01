<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-027：Editor 使用单窗口 Docking 工作区

- 状态：已接受
- 日期：2026-09-01

## 背景

ADR-017 已确定 Dear ImGui 只存在于 Editor，并通过后端无关 UI Draw List 与场景共享一次渲染和
Present。0.7.0 当时明确不启用 Docking；随着 Hierarchy、Assets、Scene View、Inspector 和 Console
形成完整工具集，普通浮动窗口已无法稳定表达编辑器布局，窗口缩放和 Runtime 状态变化也容易使面板
遮挡或越界。

直接自研 GUI 会提前承担文本排版、输入法、焦点、拖放、DPI 和渲染批处理等长期成本；把 Editor
改为 Qt 等独立平台 UI 又会改变窗口、交换链和渲染组合边界。当前需求首先是可靠的工具工作区，而非
替换整个 GUI 技术栈。

## 决策

- Editor 继续使用 Dear ImGui，并锁定官方 Docking 分支的精确提交；不跟随浮动分支。
- 使用单个主平台窗口和全屏 DockSpace，不启用 Multi-Viewport。场景与 UI 仍遵循 ADR-017 的单次
  提交和 Present，不增加第二套 Renderer 或 Swapchain。
- 新增内部静态库 `gneiss_editor_ui`，负责 ImGui Context、主题、字体、DockSpace、固定命令栏、布局
  策略和重复控件；业务面板及其状态仍由 Editor 应用层拥有。
- `gneiss_editor_ui` 不安装、不导出，不进入公共 C/C++ API，也不被 Engine、Runtime 或游戏模块依赖。
  Dear ImGui 类型只能出现在 `apps/editor` 私有边界内。
- 布局属于用户偏好，不属于工程或场景资产。布局保存在现有跨平台用户配置根下，按规范化工程路径
  的稳定摘要隔离；工程仓库内不生成 `imgui.ini`。
- Gneiss 维护布局外层版本、文件大小和原子写入规则，内部 Dock 节点状态仍由 Dear ImGui 解析。
  缺失、损坏、未知版本、只读或不可见布局统一回退到代码生成的默认工作区。
- 提供显式“恢复默认布局”命令。默认布局是可测试的产品行为，不依赖开发机历史状态。
- Editor UI 与未来游戏内 GUI 分为两套职责：前者是内部工具工作区，后者需要资产化、序列化、动画和
  Runtime 输入语义，不能因共用渲染 Draw List 而合并为同一模块。

## 影响

- 现有 UI Draw List、Texture RID、输入和帧组合边界保持不变，Docking 主要影响 Editor 私有布局。
- 面板获得拖动、拆分、吸附和标签页能力，布局可按工程恢复，当前浮动窗口绝对坐标逐步移除。
- 项目增加对 Dear ImGui Docking 分支精确提交的维护责任，升级时必须重新验证布局迁移和 Draw List。
- `gneiss_editor_ui` 只统一基础设施，不试图屏蔽全部 ImGui API，避免形成难以维护的平行 GUI 框架。
- Multi-Viewport、游戏内 GUI、插件 ABI、多语言和完整字体管理仍需各自计划与真实使用场景。
- 预期不需要 Granit 改动；若 Spike 发现通用渲染缺口，按跨仓库规则向用户提出最小 PR 需求，未经
  单独授权不直接修改 Granit。

## 替代方案

- **继续使用普通浮动窗口**：实现成本低，但无法解决布局稳定性、吸附和恢复问题。
- **立即自研 GUI**：长期控制力强，但当前会显著推迟引擎和编辑器功能，且需求尚不足以验证设计。
- **使用 Qt、Slint 或其他保留模式工具包**：成熟控件较多，但会引入新的窗口与渲染集成路线，并与
  当前场景合成方式产生较大迁移成本。
- **启用 ImGui Multi-Viewport**：可获得原生多窗口，但平台窗口、交换链、输入和 DPI 生命周期明显
  扩大，超出本版本的单窗口工作区目标。
- **把布局写入工程目录**：便于团队共享，但会污染工程内容、在只读工程中失败，并混淆用户偏好与
  可版本化项目数据。
