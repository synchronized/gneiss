<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-171：Render Scene 资源镜像

## 结果

Render Resource Service 已把 Mesh、Material 和 Texture 存储改为不可变共享资源对象。Frame Packet
按 RID 捕获共享引用，普通帧不再复制 Mesh 顶点/法线/索引和 Texture 像素正文；复制量统计只计算
动态实例、UI、Debug Draw 和资源引用。

销毁 RID 会让后续资源查询立即失效，但已经进入 Frame Packet 的共享对象持续存活到渲染完成。
Granit Render Service 仍独占 GPU 镜像：新 generation 首次出现时创建投影，旧 RID 不再被帧引用后
释放；创建失败不会修改现有有效投影。

## 验证

- `gneiss.render_resource_service`：资源创建、类型、generation 和销毁语义保持通过。
- `gneiss.render_frame_packet`：捕获对象与资源服务共享同一不可变对象，RID 销毁后仍可读取提交快照。
- `gneiss.ui_draw_list` 和 `gneiss.render_executor` 保持通过。
- Windows Clang Debug Engine 在警告视为错误条件下构建通过。

## 边界

- 本里程碑镜像稳定资源正文；动态 Transform、可见实例、UI 和 Debug Draw 仍属于逐帧 Packet。
- 当前资源是创建后不可变对象，热重载使用新 RID/generation 发布，不提供原地可变共享状态。
- 哈希表引用条目仍有少量每帧维护成本，后续可依据性能记录改为紧凑引用表。
