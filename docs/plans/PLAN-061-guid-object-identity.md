<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-061：对象身份统一为 guid

## 状态

已完成。决策见 [ADR-094](../decisions/ADR-094-guid-object-identity.md)。

## 目标与边界

场景、Prefab、对象、父节点、来源节点及覆盖键的 UUID 使用 guid；对象索引与编辑命令同步迁移。
名称、URI、文件路径仍是字符串。文件和 IPC JSON 保留规范小写 UUID 文本，不修改格式版本。
不添加字符串隐式转换或第二套身份系统。

## 实施与验收

- [x] 描述、运行时身份、查询、索引、快照和 Prefab 覆盖使用 guid。
- [x] 编辑命令、UUID 生成、层级面板与 IPC 编解码完成边界转换。
- [x] 必填身份拒绝 nil；可选父节点使用 optional，nullopt 表示根；稳定排序按字节。
- [x] Debug 编译、UUID 往返和非法输入、场景/Prefab/编辑器相关短测试通过。

不运行大场景、GPU、耗时 CI；不操作远端。

## 验收记录

2026-10-29：Windows Clang Debug 完整编译通过；41 项相关短测试全部通过。
覆盖场景与 Prefab 往返、身份修改保存、nil/非规范 UUID 拒绝、父关系、稳定排序、
重载与覆盖、编辑历史、层级操作和 IPC 节点往返。编译日志与测试日志位于本地 build 目录。
未运行大场景、GPU、浏览器构建和远端 CI；内部 C++ 签名变化需要调用方同步使用 guid。
