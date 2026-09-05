<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-165：GPU 所有权与渲染线程迁移

## 结果

Granit Render Service 已接入常驻渲染执行器。Renderer、Surface、Swapchain、Pipeline、资源镜像、
上传、命令录制、提交、Present 与销毁均由同一个渲染线程串行执行。Application 主线程只生成并
移动提交自有 Frame Packet，不再直接调用 Granit GPU 接口。

初始化和关闭使用不可丢弃的 Command，并在调用方等待排空边界。逐帧执行结果通过完成队列异步
返回；过期帧可在执行前替换，交换链重建请求会合并到下一次提交，不借用已经释放的帧内存。

## 验证

- Windows Clang Shared 增量构建通过。
- 渲染执行器与帧包单元测试通过。
- Granit 平台 Smoke、Lantern Gallery Runtime、Editor 工程与编辑工作流测试通过。

## 已知边界

- CPU 资源正文当前仍随 Frame Packet 深拷贝，资源修订命令与失败原子切换由 M-166 完成。
- 主线程在下一帧提交时消费异步失败；M-166 将补充运行中进度快照和更完整诊断。
