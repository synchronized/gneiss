<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-114：Runtime 属性编辑恢复与过载实施记录

## 结论

M-114 已完成。属性编辑面对冲突、乱序、重复、超时、断线、洪峰和连续 Runtime 会话时保持有界，
不会自动覆盖 Runtime 权威值或跨会话重放命令。

## 已验证行为

- Runtime 修订冲突返回当前修订号，不执行 setter，目标属性保持上一次成功写入的值。
- Editor 以会话 ID 和命令 ID 关联结果；不同属性可乱序完成，重复、迟到及旧会话结果被忽略。
- 单属性超时后解除在途限制但不自动重发；断线时所有在途命令转为断线状态。
- 新 Runtime 检查会话清除旧属性状态并从命令 ID 1 重新开始，不沿用旧对象或修订号。
- Runtime 每次 `pump` 最多向主线程交付 32 条属性命令，超出预算的命令收到 `not ready` 响应。
- 40 条属性命令后的 Stop 仍可被处理并进入 stopping，主线程不会先执行该批属性写入再退出。
- 既有进程测试继续覆盖 Runtime 异常退出、握手与心跳超时、强制停止及连续两次 Play。

## 测试结果

- Runtime IPC 会话测试覆盖属性洪峰、批次上限与 Stop 可达性。
- Runtime 属性执行器测试覆盖冲突前后修订号及目标值不变。
- Editor 属性状态测试覆盖乱序、重复、迟到、旧会话、超时、断线和新会话。
- Windows Clang Shared Debug 全量构建通过，CTest 106/106 通过。
- 公共 C ABI 与 IPC 协议版本均未变化。

## 后续边界

M-115 将完成 Lantern Gallery 端到端验收、Windows 本地验证以及获授权后的 Linux、Granit Runtime
和 Sanitizer 手动 Actions。远端操作不属于本记录。
