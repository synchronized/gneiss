<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Render 多视图接入核查

日期：2026-11-12。核查对象为仓库当前锁定的 Granit 源码与 Gneiss backend；未运行 GPU。
本记录是源码核查结果，不代表 Gneiss 已支持多视图。实施清单见
[PLAN-115](../plans/PLAN-115-persistent-render-scene.md)。

## 已有底层能力

Granit 的 `include/granit/pipeline/render_pipeline.h/.hpp` 已提供 `first_view`、`view_count`
及 `outputs`，可将场景快照的多个 View 渲染到独立纹理视图。
`src/pipeline/render_pipeline_api.cpp` 校验 outputs 数量等于 view_count，并逐 View 调用绘制。
带非空 Swapchain Frame 的调用只允许一个 View；无 Frame 时为离屏提交。
因此不能将多个 View 直接交给现有窗口绘制调用；可在消费端分别提交离屏输出和窗口输出。

`src/scene/scene_visibility.cpp` 已按 View 的投影矩阵与层掩码筛选球形包围体。
无需在 Gneiss 再维护一套视锥筛选或空间索引。没有发现必须先修改 Granit 的能力缺口；
资源同步、目标尺寸和 GPU 生命周期仍需后续真实运行验证。

## Gneiss 当前限制

- `frame_request` 有 views 数组，但 `detail/render_scene_state.cpp` 拒绝多于一个 View。
- `detail/frame_packet.hpp` 只有一份 draw_scene；backend 每帧构造一个 Granit scene_snapshot。
- backend 的截图路径已能创建离屏纹理并回读，但目标是单次局部对象，不是持久 target RID。
- `release_invalid_meshes/materials/textures` 以当前帧资源快照为存活集合。
  若直接逐 View 调用现有 draw，其他 View 的镜像可能被清理后重新创建；不能把这种循环当作完整多视图实现。
- 当前包围球半径逐实例扫描所有网格顶点。结果保守，但多视图会放大这部分成本；
  该优化不应阻塞正确性接入，也不能通过只按主相机过滤来规避成本。

## 接入顺序

1. 先落实 target RID 的尺寸、格式、销毁及在途引用规则，再让 View 指向目标。
2. 一个消费帧捕获全部 View 及其共享资源版本；收集资源并集后统一准备和回收镜像，不能每 View 清理一次。
3. 后端按场景与输出组织 Granit 快照和绘制；离屏输出与窗口提交分开，窗口每帧获取/呈现一次。
4. 用同场景双相机、不同场景独立目标、目标销毁时仍有在途帧验证正确性。
5. 最后接入编辑器预览和异步回读，并记录实际 GPU 验收结果。

本次没有改动 Granit，也没有增加暂时没有消费者的 target API。
