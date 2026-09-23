<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-238：Runtime 资产更新重试阶段记录

## 范围与结果

2026-09-23，完成 Runtime 异步热更新失败后的显式重同步入口，M-238 整体仍在实施。
Asset Browser 使用既有失败状态显示重试按钮，复用既有全量同步队列，重新提交已知资产。
仅已连接且运行或暂停的 Runtime 可重试；等待、执行中、已成功以及要求重启的状态不重复排队。
要求重启的变更显示停止并重启提示。本次不修改公共 ABI 和 Granit。

## 验证

- Windows Clang Debug：Editor 与 runtime-process 测试目标构建成功。
- `gneiss.editor.runtime-process`、`gneiss.editor.smoke`、
  `gneiss.editor.lantern-runtime-workflow`，3/3 通过。
- 真实 Runtime 使用临时工程副本：写入非法材质触发失败，未修复重试仍失败；恢复文件后
  重同步成功，进程保持运行。覆盖未连接、重复请求和成功后请求的拒绝行为。
- 已格式化并运行 clang-tidy；检查仍报告异常逃逸、初始化列表、位运算及函数复杂度告警，
  测试主函数复杂度随新增分支增加；未降低检查等级。

## 剩余范围

发布资产修订的同步返回错误反馈及事件丢失后的自动补扫尚未完成。
文件监听反馈与重启见下方阶段补充。
本次未进行按钮人工点击、Linux/MSVC 矩阵或发布验证，也未注入内存分配失败。
重同步会处理当前进程已知的全部资产；不扫描磁盘发现未知资产。


## 文件监听反馈与重启阶段补充

2026-09-23，Asset Browser 增加源文件与作者资产监听错误的目录、原因及重启入口。
可选的 sources 目录不存在时仍显示等待状态，保持原有定时尝试；不创建空目录。
事件队列溢出显示累计丢失数量与手动检查提示，不将重启监听表述为恢复漏掉的更新。

修复 `uv_fs_event_start` 失败路径提前释放正在关闭句柄的问题：容器分配在句柄初始化前完成，
初始化后的句柄由监听器保留到事件循环关闭结束。重启到有效目录时丢弃上一轮未消费事件，
丢失计数在对象生命周期内累计，避免被重启清零掩盖。

验证：Windows Clang Debug 构建 Editor 与监听测试成功；`asset-file-watcher`、
`asset-reimport-queue`、`editor.smoke` 共 3/3 通过。新增回归覆盖停止后切换目录、
旧事件隔离、累计计数保留及新事件接收。clang-tidy 仍有既有回调异常逃逸、位运算等告警。
未进行按钮人工点击、Linux/MSVC 验证或底层启动失败的确定性故障注入。
