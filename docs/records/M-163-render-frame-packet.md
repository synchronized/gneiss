<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-163：帧包与同步执行边界

## 结果

Application 的渲染提交已收敛为自有 `render_frame_packet`。帧包复制窗口状态、Scene 渲染快照、
UI、Debug Draw 以及该帧引用的 Mesh、Material 和 Texture CPU 数据；Granit Render Service 不再
回读主线程的 Scene/ECS、即时绘制列表或资源 RID 表。

渲染暂时仍在调用线程同步执行，因此本阶段只改变数据所有权，不改变帧调度。窗口重建请求通过帧包
结果显式合并回平台状态。原生窗口描述被拆到不依赖 Granit 头的内部文件，帧包与单元测试不会暴露
第三方类型。

## 验证

- 新增帧包测试，覆盖捕获后清空 Debug Draw、销毁源 Mesh/Material/Texture，帧包内容仍完整有效。
- Editor Smoke、Lantern Gallery 与 Granit 平台 Smoke 通过，保持同步渲染行为。

## 已知边界

- 当前为保证所有权明确，会复制该帧引用的 CPU 资源正文；M-165 将以渲染命令和后端镜像修订减少
  稳定资源的逐帧复制。
- 帧调度仍同步；异步队列与完成回执由 M-164 引入。
