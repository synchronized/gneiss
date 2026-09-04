<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-147：Asset IPC 协议域实施记录

## 结果

IPC v2 新增版本 1 的 `asset` 域，并加入标准域能力协商与统一注册表。该域包含两个请求/响应操作：

- `reload`：Editor 通知 Runtime 应用一个已提交的增量资产修订。
- `resync`：Editor 在新会话或状态不一致时发送完整的当前资产修订集合。

两个操作共用事务负载：会话 ID、非零单调修订号，以及最多 1024 个 URI 与资源类型。首批资源类型
限定为 Texture、Material 和静态 Mesh。协议不传输资产正文，Runtime 后续仍通过工程 VFS 读取已
提交产物。

Runtime 以同一请求 ID 返回整个事务的权威结果，状态为已应用、失败、旧修订或需要重启。URI 必须
为无父目录跳转的 `asset://` 地址，单次负载上限为 256 KiB；重复 URI、未知类型、错误方向、错误
消息种类和零请求 ID 均会被拒绝。

## 边界

本里程碑只建立协议、能力协商和编解码规则，不在 Editor 或 Runtime 会话中发送、处理该域消息。
Runtime 的安全点事务应用由 M-148 实现，Editor 的修订发布和结果展示随 M-149、M-150 接通。

## 验证

- Windows Clang Shared Debug：10 项 IPC 协议、信封、分发与传输测试通过。
- Windows Clang Static Debug：相同 10 项测试通过。
- Asset 专项测试覆盖增量通知、全量重同步、请求关联、结果状态、重复 URI 与错误消息种类。
