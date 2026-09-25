<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Editor 选择、变换与窗口交互

## 选择与变换

在 Scene Hierarchy 选择节点后，在 Scene View 使用 Move、Rotate 或 Scale。Move/Rotate 使用世界轴，
Scale 由 ImGuizmo 按节点局部轴处理。拖动预览实时写回作者场景，释放后产生一条撤销记录；
切换或清空选择、折叠视图时会收尾原节点的拖动，不把操作转移到新选择。
命令记录失败时恢复原节点拖动前的值。

支持普通场景节点和 Prefab 来源子节点；Prefab 实例根当前使用 Inspector 编辑。
父级正非均匀缩放遵循 Scene Tree 的显式 TRS 组合，不能表达任意剪切矩阵。
负缩放、零缩放或无效 Gizmo 矩阵不能用于此拖动路径；失败不覆盖作者变换。

当前选择入口是层级树。Scene View 的命中检测针对 Gizmo 控件，**尚无点击模型表面选择对象的完整
几何拾取**，也不提供遮挡选择或 ID Buffer 拾取。不要把 Gizmo 轴命中视为已经实现场景拾取。

## 窗口与坐标

当前场景渲染覆盖整个窗口，Scene View 负责裁剪显示和输入区域。Gizmo 使用全窗口纵横比，
移动或调整面板不会创建独立的场景渲染目标。

原生 Win32 路径的窗口渲染尺寸和鼠标坐标使用客户区物理像素，ImGui framebuffer scale 为 1。
不能再按显示器缩放比例单独乘鼠标坐标，否则会产生重复缩放。零尺寸期间 UI 使用最小 1×1 尺寸，
恢复窗口后重新采用有效客户区尺寸。该约定不意味着字体已实现随 DPI 自动重建或物理大小恒定。

## 回归验证

配置并构建 Editor 后运行：

```powershell
ctest --preset windows-clang-debug -R 'gneiss.editor.(transform-gizmo|gizmo-input-replay|native-window-interaction)' --output-on-failure
```

跨平台输入回放驱动真实 ImGuizmo 控件；事务测试验证 Scene Tree 写回及 Undo/Redo。
Windows 原生窗口测试覆盖连续 resize、最小化恢复及合成 DPI 消息，验证窗口尺寸、指针状态和 UI 提交。
离屏窗口收到真实鼠标离开消息时，测试要求 ImGui 同样报告指针不在窗口内。

这些测试不验证 GPU 截图、主观拖拽手感或物理多显示器跨屏行为；完整覆盖范围与限制见
[0.37 验收记录](../records/M-242-245-0.37.0-validation.md)。
