<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# ADR-094：内存对象身份使用 guid

## 状态

已接受，实现进度见 [PLAN-061](../plans/PLAN-061-guid-object-identity.md)。

## 背景与决策

字符串 UUID 使同一身份在描述、对象索引和编辑命令中反复复制与校验。
统一使用现有 engine::guid 保存 16 字节值，不另设身份所有者。场景、Prefab、对象 UUID
以及复合作者键均使用 guid；parent_uuid 使用 optional<guid>。必填值不能为 nil。

文件和 IPC 的 JSON 字段继续保存小写、带连字符的 UUID；只在文本边界解析和格式化。
排序采用标准 UUID 字节序，保持与规范文本排序一致。名称、URI、路径保持字符串。

## 影响与替代方案

调用者必须显式解析外部文本，减少内存分配并消除非规范文本导致的身份不等问题。
文件格式不变，但内部 C++ 签名改变；UI 显示、测试夹具和 IPC 编解码须同步迁移。
不采用字符串隐式转换，以免掩盖错误输入或在内部调用链重复解析。
