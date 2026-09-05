<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-167：窗口恢复与确定关闭

## 结果

零尺寸窗口暂停生成渲染帧。Resize 与交换链 Out-of-date 状态通过异步帧回执返回，并在下一帧提交
前合并；若该次提交失败，Application 会保留主线程尚未消费的重建请求，避免窗口恢复事件丢失。

关闭过程先排空已有 Frame，再以不可丢弃 Command 在渲染线程逆序释放全部 Granit 对象，随后停止
线程并 Join。初始化中途失败也会在该线程执行配对清理，GPU 对象不会转移到调用线程析构。

## 验证

- 帧回执测试覆盖交换链重建状态传递和积压帧替换。
- 执行器测试覆盖排空、失败后继续运行、停止、Join 与重复停止。
- Granit 平台 Smoke 覆盖真实窗口创建、帧执行和资源零泄漏关闭。
- Lantern Gallery Editor 启动、Runtime 工作流和关闭测试通过。

## 已知边界

- Surface Lost 的平台专用重建策略仍由 Granit 抽象处理；Gneiss 只维护排空与重试边界。
- 窗口持续拖动时允许替换尚未执行的旧 Frame，不保证展示每一个中间尺寸。
