<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Render 独立请求与产品回归验收

## 范围

对应 [PLAN-115](../plans/PLAN-115-persistent-render-scene.md) 前四组收尾。
Granit 保持 0.73.2，本轮未修改上游。当前接口契约见 [Render Reference](../reference/render.md)。

## 实现变化

- 截图返回 capture_request，执行线程发布结果；取消只放弃结果，不中断 GPU。
  请求可跨服务关闭存活，成功结果只能领取一次；删除公共 required completion 队列。
- 准备失败、绘制失败同样执行请求终态通知；关闭排空已接受工作。
- mesh_instance_description 归 render；删除无使用者的 render_snapshot 诊断项。
  material_data 是资产装配/Render 共用的 CPU 负载，保留现有类型。

## Windows shared Debug 结果

最终 shared 构建成功，17 项定向测试全部通过（2026-11-21）。static 构建及 7 项渲染定向测试也全部通过。

- render-capture-failure：材质要求 UV1、网格没有 UV1，真实 Backend 返回 invalid_argument；
  错误传到 Application，宿主 on_stop 执行，服务关闭，所有已接受截图具有终态。
  这是普通后端错误，不是设备丢失模拟；重复 shutdown 的零计数不作为首次关闭零泄漏证据。
- render-pressure：预算拒绝保留帧、重试、独立截图取消/领取、重复领取拒绝、关闭后领取最后图像；
  正常关闭的 GPU 活资源计数为零。取消与完成竞争时，先完成的结果不会被覆盖。
- render_executor、render_scene、ui_draw_list、native-render-lifecycle：队列、准备失败通知、
  场景/目标身份以及生命周期回归。
- scene-viewer-dual：双视图独立相机/尺寸与窗口合成像素。
- editor.session、editor.transform-gizmo-drag：生产编辑会话、撤销重做与 Gizmo 修改。
- editor.runtime-scene-loading、editor.asset-workflow：真实 Runtime 进程场景切换、资产导入/
  重导入通知及关闭；不是仅用查看器替代产品接入。
- editor.transform-gizmo-view、editor.native-window-interaction：Gizmo 视图与原生窗口尺寸/输入链路。
- asset-pixel、render-texture-uploads：纹理、材质、阴影及异步上传像素回归。
- editor.smoke、editor.project-manager-smoke：Editor 项目及项目管理入口启动关闭。

## Static Debug 与检查

静态构建通过 scene-viewer-dual、render_executor、render_scene、asset-pixel、
render-pressure、render-texture-uploads、render-capture-failure 共 7 项。
未声称 static 下重新执行 Editor 产品矩阵。clang-format、git diff --check 和本轮文档相对链接检查通过。
最后仅调整 render_scene.hpp 的 include 排序，未改变接口或实现，不重复执行 GPU 矩阵。

## 限制

- 尚未执行完整手工 GUI 连续操作；分项用例不等同于所有产品交互都已验收。
- 拾取没有实现；Granit Pipeline 未公开对象 ID 输出/拾取入口，语义待用户确认。
- 真实设备丢失、GPU 饱和预算定标、其他硬件和浏览器非阻塞消费未验证。
