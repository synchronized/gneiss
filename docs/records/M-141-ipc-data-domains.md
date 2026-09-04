<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-141：IPC 数据域协议实施记录

## 结果

已为 Log、Inspection、Statistics 和 Property 建立独立 v2 域接口与方向规则，上层组合不再需要使用
全局消息编号识别这些数据。M-142 双端切换前，v2 适配层暂时复用已验证的 v1 域内 JSON 编解码；
全局帧类型和冗余适配会在双端切换后统一删除。

## 域行为

- Log：Runtime→Editor 的结构化日志事件，保留现有 16 KiB 单事件上限。
- Inspection：Runtime→Editor 的可分片快照事件，以及 Editor→Runtime 的重同步请求。
- Statistics：Runtime→Editor 的统计快照事件，保留会话 ID 和递增序号。
- Property：Editor→Runtime 写入请求及 Runtime→Editor 响应，共用同一个信封请求 ID。

Property 的旧 JSON 负载在迁移阶段仍包含 `command_id`。适配层要求它与 32 位信封请求 ID 完全一致，
避免出现两个关联来源；M-142 删除 v1 编解码时同时删除该冗余字段。

## 验证

- 覆盖日志和统计快照往返。
- 覆盖检查批次、分片封装和重同步请求。
- 覆盖属性写入与结果往返、请求 ID 复用及关联不一致拒绝。
- 覆盖四个域的操作数量、方向和消息语义声明。
- 继续运行旧数据协议测试，确认迁移适配未改变负载、顺序戳及分片行为。
