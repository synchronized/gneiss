<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-021：Transform Gizmo 与显式 TRS 边界

- 状态：已接受
- 日期：2026-08-28

## 背景

Gneiss Scene Tree 持久化局部平移、四元数旋转和逐轴缩放，不持久化任意矩阵或剪切。Editor 需要在
世界空间显示并操作 Gizmo，同时把结果稳定写回局部 TRS。父级非均匀缩放、负缩放、矩阵排列和 UI
输入占用若没有明确边界，会造成视图结果、场景作者值与 Undo/Redo 不一致。

## 决策

- 层级组合继续以 Gneiss 显式 TRS 语义为权威；Gizmo 不引入第二套矩阵场景树。
- 世界 TRS 写回局部值时直接反演 Scene Tree 的组合公式：逆父旋转后除以父缩放，局部旋转使用
  `inverse(parent) * world`，局部缩放逐轴相除。非均匀正缩放已有独立往返测试，不依赖通用矩阵
  分解。
- 首版拒绝带负缩放的 Gizmo 编辑，因为矩阵分解无法稳定恢复反射轴；已有场景仍可显示和通过
  Inspector 编辑数值。零缩放和非有限值继续按 Transform 契约拒绝。
- Editor 采用 [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) 的单个
  `ImGuizmo.h/.cpp` 控件，固定提交
  `18cef5e031d8c6973d80284c67f60549fafd78c1`，仅链接可选 Editor 目标。该库为 MIT 许可，依赖 Dear
  ImGui 且不进入 Runtime、安装接口或公共 ABI。
- Gneiss 渲染数学与 ImGuizmo 均使用 column-major 数组和列向量；Editor 适配层显式构造模型、视图
  和投影矩阵，不依赖渲染内部私有类型。Vulkan 投影的 Y 方向与深度范围保持现有 Camera 约定。
- 当前场景由全窗口 Swapchain 渲染，因此 Gizmo 与世界网格使用全窗口显示区域计算投影；Scene View
  面板只负责裁剪绘制和接收输入，不得用面板局部宽高重新计算相机纵横比。
- Gizmo 激活时消费指针输入，Editor Camera 不响应同一拖动。激活时保存初值，拖动期间只更新预览，
  释放时记录一条可合并的 Transform 命令；失败则恢复激活前值。

## 影响

- 非均匀正缩放父级可以稳定编辑，不需要把剪切或矩阵加入场景 Schema。
- 负缩放对象首版会显示明确的不可编辑提示，避免静默改变反射轴。
- ImGuizmo 仅解决命中、绘制和交互，不拥有场景状态、命令历史或持久化。
- 当前所需能力均位于 Gneiss Editor 和既有 UI Draw List 内，不需要修改 Granit。

## 替代方案

- **自行实现 Gizmo**：可以完全控制行为，但轴命中、屏幕尺寸、遮挡和交互细节成本较高。
- **持久化任意矩阵**：能表达剪切，却会扩大 Scene Tree、反射、序列化和物理边界，不符合当前需求。
- **对任意世界矩阵直接分解**：实现短，但负缩放和剪切下结果不唯一，会破坏作者值稳定性。
- **只提供局部空间 Gizmo**：数学简单，但不能满足常见的世界轴平移和旋转工作流。
