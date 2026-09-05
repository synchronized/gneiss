<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-161：结构热重载示例与跨平台验收

## 结果

Lantern Gallery Runtime 工作流改为复制到隔离的临时工程后执行真实结构修订，覆盖以下闭环：

- 修改当前 Scene 名称并验证匹配 Runtime 对象身份保持。
- 修改 Prefab 来源并验证同源实例批量刷新、匹配身份及实例局部覆盖保持。
- 写入损坏 Prefab 并验证修订失败、既有 Runtime 场景完整保留。
- 恢复有效 Prefab 并验证后续修订可以继续应用。
- 停止并重新启动 Runtime，验证新会话重新同步且旧会话编辑状态不泄漏。

## 验证

- Windows Clang Shared 全量构建及 129 项测试通过。
- Windows Clang Static 全量构建及 127 项测试通过。
- Linux 手动 Actions 运行 `33949249237` 通过：Clang/GCC Shared/Static、Granit Runtime
  Shared/Static 及 Sanitizer 共七项任务全部成功。
