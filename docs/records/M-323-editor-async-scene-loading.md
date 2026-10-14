<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-323：编辑器异步场景加载接入

日期：2026-10-13。范围为 0.50 当前分支追加的启动和 File/Open 接入，未发布。
当前交互以 [Editor 指南](../guides/editor-interaction.md) 为准，实施范围见
[DEV-050](../plans/DEV-050-main-loop-latency.md#editor-异步加载接入)。

## 实现与验证

- 使用既有 Scene Load Service 和宿主执行器，启动先建立空文档与相机，主循环推进异步候选。
- 加载界面展示阶段和阶段计数；取消、失败重试、回到原文档；成功后接管新实例并重绑相机及编辑状态。
- `gneiss.editor.scene-loader` 覆盖线程池和协作模式，失败/取消保留文档与历史、重复请求、再次切换、
  Prefab 枚举、编辑/Undo/Redo、旧 World 失效和在途关闭。
- C++ 场景接管包装测试覆盖已有输出、跨 Application、错误线程、release/adopt 后编辑；C ABI 未变。
- Windows Clang Release 相关测试 13/13 通过；补充 Prefab 用例后 scene-loader 再次通过。
  C11/C++20 头文件编译通过，静态/共享核心重建及安装消费者分别 4/4 通过。
  新协调器 clang-tidy 无自有代码告警；未重新宣称整个既有 Editor 无静态检查告警。
- 实际编辑器 `--project D:\sunday\workspace\asset-cache --smoke` 打开完整 Sponza：
  输出 `Editor scene ready: nodes=553, prefab_nodes=0`，运行后退出码 0，stderr 为空。
  日志：`build/0.50-editor-sponza-fixed.{out.log,err.log,exit.txt}`。
  随后的 editor_demo 冒烟同样退出 0，2 个节点。

## 实机暴露的关闭顺序缺陷

首轮 Sponza 在进度界面之后停止响应。采样主线程处于线程 join，文件 I/O loop 仍在等待事件；
仅凭屏幕最后显示的复验阶段不能判断阻塞发生在复验中。代码检查发现 Application 先关闭文件
执行器，再释放渲染端纹理快照；后者仍可持有 texture container 的文件源，造成关闭等待。

将文件执行器停止移动到渲染服务释放之后。首轮进程为本次测试启动的 PID 2632，保留
`build/0.50-editor-hang-stacks.txt` 后终止；不能把该轮记为通过。修正后完整 Sponza 冒烟退出 0。
截图 `build/0.50-editor-progress.png` 是首轮实际加载界面，不是性能证据。

## 限制

这是功能及生命周期验证，未采集本轮输入延迟分布，也未完成物理 DPI/跨屏、实机按钮自动回放。
工程恢复和图形初始化仍在加载界面之前；作者事务回滚、文件监视结构重载未迁移。
本次新增 Editor 接入未完成新的 Linux 验证；先前 Runtime Actions 仍在运行，不计为本次通过。
0.50 既有输入延迟超标及发布门禁仍以 [M-321](M-321-0.50.0-release-validation.md) 为准。
