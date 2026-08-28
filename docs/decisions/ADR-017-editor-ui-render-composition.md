<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-017：编辑器 UI 与场景渲染组合边界

- 状态：已接受
- 日期：2026-08-28

## 背景

Editor 需要在场景画面上绘制层级、属性等工具面板。当前 Granit Render Service 独占 Renderer、
Swapchain、帧获取、命令提交和 Present；若 Editor 再创建一套渲染状态，将产生窗口与交换链所有权
冲突。反过来让 Runtime 直接依赖 Dear ImGui，又会把特定工具 UI 技术带入运行时核心。

## 决策

- Granit Render Service 继续唯一拥有 Renderer、Swapchain 和单帧提交。每帧先绘制场景，再在同一
  颜色目标上绘制 UI，最后只提交和 Present 一次。
- Runtime 提供后端无关、仅当前帧有效的 UI Draw List 契约，内容限定为顶点、`uint32_t` 索引、
  Texture RID、裁剪矩形和绘制范围。提交时由 Runtime 深拷贝，调用方内存在返回后即可释放。
- `apps/editor/` 拥有 Dear ImGui Context、面板和输入适配，并将 `ImDrawData` 转换为 Gneiss Draw
  List；Runtime 和公共接口不包含 ImGui 类型、头文件、标识或回调。
- UI 纹理由所属 Application 的 Texture RID 表示。Font Atlas 也通过公开 Texture API 创建，不允许
  将后端 Texture View、Descriptor 或裸指针编码进公共接口。
- Granit 后端复用 `granit::canvas_draw_list`：其 Canvas Pipeline 开启 Alpha 混合、不使用场景深度
  附件并逐绘制命令设置 Scissor。动态顶点和索引数据使用 Canvas 的逐帧缓冲，不与场景几何 Arena
  混合；Gneiss 只负责 Texture RID 映射和数据适配。
- UI Draw List 只能在 Application 创建线程、当前 update 回调内提交；每帧最多保留最后一次成功
  提交，渲染完成或跳过渲染后立即清空，不跨帧缓存。
- `0.7.0` 只支持单窗口和单 Viewport，不启用 Dear ImGui Multi-Viewport 或 Docking。输入由 Editor
  消费公开 Gneiss 输入事件，并根据 UI 捕获状态决定是否继续处理编辑器 Camera 操作。

## 影响

- Editor 与游戏场景共享一套 GPU 和帧生命周期，不会争用同一窗口的交换链。
- Runtime 增加一项可复用的即时 UI 数据能力，但仍不知道具体 UI 库；未来调试 HUD 可复用该契约。
- Draw List 深拷贝带来 CPU 复制开销，首版优先保证 ABI、生命周期和失败原子性；性能数据出现后再
  评估映射缓冲或构建器接口。
- 当前 Granit Canvas 已具备动态几何上传、纹理、Alpha Pipeline、Scissor 和索引绘制能力，不需要
  修改 Granit。若后续缺少可复用底层能力，仍按跨仓库协作规则向用户提出最小 PR 建议。

## 替代方案

- **Editor 单独创建 Renderer 与 Swapchain**：边界看似独立，但会争用窗口和 Present 生命周期。
- **Runtime 直接集成 Dear ImGui**：实现较快，但游戏运行时被迫依赖工具 UI，且公共边界难以替换。
- **暴露 Granit Renderer 或命令录制器**：灵活但泄漏后端类型，破坏 Service 隔离和 C ABI。
- **先渲染 UI 到第二窗口**：能绕过同帧组合，但不满足首版单窗口编辑器闭环。
- **在 Editor 中复制完整场景渲染器**：造成第二套资源镜像与渲染状态，维护成本和一致性风险过高。
