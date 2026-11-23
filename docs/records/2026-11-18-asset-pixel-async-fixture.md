<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 像素验收的异步加载夹具修正

## 原因与修改

承接[测试分类验收](2026-11-18-test-classification-build-acceptance.md)中的 asset-pixel 失败。
检查渲染快照确认纹理内容仍为空，后端使用默认白色贴图，真实纹理尚未上传。
材质加载只需取得纹理的稳定引用，不保证纹理依赖已完成。测试使用协作式执行器，
等待颜色时只驱动 Application，没有继续 run_ready，因此纹理解码任务不能完成。

本轮只修改 tests/engine/render/asset_pixel_test.cpp，不修改生产 API、后端或 Granit：

- 热重载、mip 和场景初始像素等待同时驱动任务调度与宿主更新，保持原颜色阈值及超时。
- 场景重开以实际蓝色纹理作为基线，避免将默认白色贴图保存为“已加载”的基线。
- 材质通道和状态对照先等待纹理请求成功，再完成一次回读以跨过上传提交，
  随后的回读采样真实纹理；不再以固定运行三帧代替 CPU 请求完成。
  此局部辅助只用于夹具的线程池执行器，不提供生产同步加载接口。
- 交还纹理 RID 后验证新查询/修改失败，同时验证已有材质依赖仍能渲染原内容。
  与 render_scene_test 的依赖保活契约一致，不再要求整帧返回 invalid_handle。

排查用的临时后端输出已移除。没有通过增加超时、降低像素阈值或跳过子流程使测试通过。

## 验证

共享 Windows Clang Debug 的 asset-pixel 完整通过（32.49 秒），负缩放定向复测通过。
静态配置 asset-pixel（31.80 秒）与负缩放均通过，2/2。日志见本地 build/pixel-fix.log、pixel-negative.log、
pixel-static.log。未执行全 GPU 集合、跨平台、压力或设备丢失验收。
