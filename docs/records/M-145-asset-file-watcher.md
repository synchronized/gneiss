<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-145：工程源资产文件监听实施记录

## 结果

Editor 资产模块新增私有 `asset_file_watcher`，递归监视工程 `sources/` 并以线程安全有界队列输出
规范化相对路径。公开头文件不包含 libuv 类型；具体后端通过既有 `uv_loop_executor` 在专用 I/O
线程运行，生命周期由监听对象独占。

事件只表达“候选变化”，保留平台给出的 changed 或 renamed 提示，不承诺一次写入只产生一个事件。
Windows 根监听使用 libuv 递归能力；其他平台为已有目录分别注册监听，并在目录重命名事件后发现
新目录。队列满时丢弃最旧事件并累计计数，后续 M-146 仍须通过防抖与内容校验确认真实变化。

## 验证

- 缺失监听根返回 `not_found`，重复停止返回 `not_ready`。
- 已有嵌套目录中的修改和重命名能够产生相对路径事件。
- 运行中新增目录及其文件能够被发现。
- 有界队列在事件洪峰下累计丢弃数量。
- Windows Clang Shared Debug 专项测试连续运行 20 次通过。
- Windows Clang Static Debug 专项测试通过。

Linux 的实际文件监听行为将在 M-150 跨平台矩阵中统一验收。
