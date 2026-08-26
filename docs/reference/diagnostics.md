<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 诊断接口

在 `gneiss_application_desc::diagnostic` 设置回调后，Application 会同步报告可定位的运行时失败。
消息包含严重度、类别、稳定结果码、来源模块和 UTF-8 文本。所有指针均由 Gneiss 持有，只在回调
期间有效；回调使用描述结构的 `user_data`，在 Application 创建线程执行。

回调可以执行只读查询，但不得重入运行、加载、销毁等修改操作，也不得让异常越过 C ABI。没有
回调或直接丢弃消息不会改变原操作的结果码。当前不保存历史记录。
