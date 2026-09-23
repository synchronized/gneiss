<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-237：重导入异常诊断与任务恢复

## 结果

重导入回调原先由整个 tick 的 catch 兜底；异常会跳过失败事件、候选删除和尝试计数，导致同一任务
每帧重复执行，也可能阻断后续候选。现在在回调边界将标准异常与未知异常转换为失败报告，补全源
路径并保留标准异常原因。失败尝试消耗本帧导入预算，任务完成后移除；修正源资产并再次 notify
可以重新尝试，不隐式无限重试。

notify 的路径校验移入异常保护，避免文件系统路径操作异常越过 noexcept 边界。
没有新增公共 API 或第二套运行时状态，既有导入事务及资产输出行为不变。

## 验证

Windows Clang Debug 编辑器构建通过；资产监听、导入、重导入队列、Editor smoke 和 Lantern Editor
共 5 项通过。新增回归覆盖标准异常诊断与源路径、失败后的预算和候选数、健康任务继续执行、
再次通知后恢复、空闲帧不重复调用、未知异常失败事件。

clang-format 与 git diff 检查通过。clang-tidy 仅剩该文件原有 due 初始化形式告警；没有通过
抑制规则隐藏本轮告警。未执行本轮 Linux、MSVC 或完整矩阵，未验证内存耗尽时诊断分配成功。

后续工作为监听错误与 Runtime 更新失败反馈，见 [0.36 计划](../plans/VER-036-0.36.0-editor-asset-workflow.md)。
