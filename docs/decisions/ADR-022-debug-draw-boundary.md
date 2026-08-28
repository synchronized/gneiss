<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-022：逐帧 Debug Draw 边界

- 状态：已接受
- 日期：2026-08-28

## 背景

Editor 世界网格使用 ImGui Overlay 时不读取场景深度，会穿过模型并削弱空间判断。Granit 已提供可
深度测试的世界 Debug Draw，但其句柄和类型不得进入 Gneiss 公共接口。

## 决策

- Application 在 update 回调内接受一份后端无关的世界线段列表，并在提交时深拷贝。
- 列表只在当前帧有效；同帧再次提交原子替换，不建立长期 RID 或第二份场景状态。
- 每条线显式选择是否深度测试，均不写入深度；像素宽度和 RGBA8 颜色属于稳定输入。
- Granit 适配层把线段转换为 `granit_debug_draw_list`，在场景之后、UI 之前使用当前 Camera 与深度
  附件录制。普通接口不暴露 Granit 类型。
- Editor 网格和世界原点轴启用深度测试；视角坐标标与 Transform Gizmo 继续作为可操作 Overlay。

## 影响

- 世界辅助线可被场景正确遮挡，Gizmo 仍不会因完全隐藏而失去操作入口。
- 接口可供后续物理、导航和诊断可视化复用，但本阶段只承诺世界线段。
- 无 Granit 后端时提交仍能完成逐帧状态验证，不改变安装接口的依赖传播。
