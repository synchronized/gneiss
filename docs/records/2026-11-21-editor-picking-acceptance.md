<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Editor 点击拾取验收

日期：2026-11-21。对应 [PLAN-115](../plans/PLAN-115-persistent-render-scene.md)。

## 本次范围

Scene View 左键提交独立 GPU 拾取，命中选择对象，背景清空选择；Gizmo 优先。
编辑器协调器借用文档、World 和 Renderer，不增加 IPC 协议或第二套对象身份。
最新点击替代旧请求；文档版本、原选择、相机、目标或尺寸变化时放弃结果。
取消只放弃消费，GPU 工作仍由 Render 服务排空。

## 验证

Windows Clang Debug 共享构建，Granit 0.74.1。
构建目标：gneiss_editor_scene_picking_test、gneiss_editor、gneiss_editor_transform_gizmo_view_test。
CTest：gneiss.editor.scene-picking、gneiss.editor.transform-gizmo-view、gneiss.editor.smoke，三项通过。

拾取测试加载真实场景、执行 GPU 回读，检查具体对象身份、背景、新点击替代、越界坐标、
外部选择变化、相机变化、尺寸变化、文档修改和关闭。选择不改变文档版本及撤销历史。
测试的 finish_frames 仅用于确定性领取结果，产品帧循环使用 ready 非阻塞检查。

## 未验证

未执行手工连续鼠标交互和高 DPI 多显示器验收；未扩展 MASK/遮挡、设备丢失、
完整取消/失败矩阵。未重复静态库与跨平台矩阵。本记录不代表 PLAN-115 全部完成。
