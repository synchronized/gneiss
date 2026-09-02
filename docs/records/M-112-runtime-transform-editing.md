<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-112：Transform 运行态编辑实施记录

## 结论

M-112 已完成。Editor 的 Runtime Inspector 现在可编辑独立 Runtime 中节点的 Transform，修改只影响
当前运行会话，不进入作者场景、撤销历史或保存流程。

## 已实现内容

- Runtime Inspector 在协商 `runtime_property_edit_v1` 后开放 Translation、Rotation 和 Scale 控件；
  未协商能力时保持只读。
- 平移和缩放使用 Vec3 传输；旋转继续以欧拉角供用户编辑，并通过既有数学函数转换为四元数传输。
- 一次拖动在控件结束编辑时提交，等待 Runtime 确认期间禁用同属性控件，避免产生无界命令流。
- 后续编辑使用 Runtime 返回的当前修订号；冲突和其他拒绝不会进行乐观覆盖。
- Inspector 展示等待、已应用、拒绝、超时和断线状态；已应用规范值与后续镜像不同时提示运行逻辑
  已覆盖该值。
- Camera 与 Mesh Renderer 在本阶段仍保持只读。

## 验证结果

- Editor–Runtime 真实进程测试覆盖运行状态下 Translation 写入、单属性在途限制与成功确认。
- 同一测试覆盖暂停状态下 Scale 写入和确认，并在成功后恢复 Runtime。
- 欧拉角与四元数转换继续由既有旋转数学测试覆盖。
- Windows Clang Shared Debug 全量构建通过，CTest 106/106 通过。
- 公共 C ABI 未变化；界面和命令状态均位于 Editor 私有实现层。

## 后续边界

M-113 将允许用户把明确选中的受支持 Runtime Transform 值应用回作者场景，并复用既有命令历史、
脏状态与保存路径。该操作必须显式触发，不能自动同步整个 Runtime 场景。
