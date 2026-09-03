<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-123 Prefab 覆盖身份与合并语义实施记录

## 结果

M-123 已建立 Scene 内部的 Prefab 字段覆盖模型。覆盖键由实例 UUID、源节点 UUID、Type ID 和
Field ID 组成；值使用拥有自身字符串和数组内存的 C++20 variant，不依赖 IPC 或 JSON 表示。

## 已验证行为

- 覆盖写入通过冻结的 Type Registry 校验字段存在、属性类别和可写能力。
- 相同键重复写入会替换旧值，不产生重复记录。
- 与来源值相同的写入会删除覆盖；正负零规范化后视为相同。
- 覆盖集合按复合作者身份、Type ID 字节序和 Field ID 确定性排序。
- 非规范 UUID、空 Type/Field ID、类型不匹配、只读字段、NaN、无穷和非法 UTF-8 被拒绝。
- 未知字段保留 `NOT_FOUND` 语义，供后续刷新事务给出明确失败原因。

长期决策见 [ADR-032](../decisions/ADR-032-prefab-property-overrides.md)。场景 v4 编解码、Runtime
投影和 Editor 呈现分别由 M-124、M-125 和 M-126 接续实现。

## 验证

Windows Clang Debug 下，新增 `gneiss.prefab_property_override` 专项测试通过；相关 Prefab、场景描述
与反射测试保持通过。实现仍是私有 Scene 能力，没有新增公共 C ABI 或安装头文件。
