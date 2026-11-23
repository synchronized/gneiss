<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 稳定属性地址与覆盖快照验收

日期：2026-11-05。环境：Windows、windows-clang-debug。

## 完成范围

- Prefab 覆盖改为目标身份与完整 Meta 路径，删除单独存储的首属性 ID。
- 路径支持字段 ID、固定序列下标、字符串映射键与序列 GUID 身份。
- 可选 sequence_identity_traits 接入场景对象 UUID；查找拒绝缺失、空身份及重号。
- 无身份可变列表使用独立 JSON 属性快照；组件编辑支持列表长度变化及撤销恢复。
- 叶值和快照以带标签联合存储，复制快照不共享可写对象；恢复到临时描述后统一校验。
- 迁移编辑入口、诊断、文件样例、测试以及内部头可见性清单。

当前行为见[场景文件格式](../reference/scene-format.md)、
[反射容器视图](../reference/reflection-containers.md)；设计边界见
[ADR-105](../decisions/ADR-105-serialization-format-boundary.md)。

## 验证

- 全量 `cmake --build --preset windows-clang-debug -j5` 通过。
- 57 项相关回归已通过：首次 54 项通过；三处旧测试假设修正后，对应三项重跑全部通过。
  覆盖 Meta、序列化、Scene/Prefab、资源、IPC、编辑器及模块边界。
- 新增用例验证插入、删除其他元素和重排后稳定定位；目标删除、空身份及重号失败；
  可变序列持久化下标拒绝；字符串映射键定位；四类路径步骤读写往返。
- 快照用例验证源数据独立、整列表替换、保存重载、恢复失败保留输出及编辑器撤销恢复长度。
- 修改 C++ 文件的 clang-format 检查和 git diff --check 通过。

未运行大型场景、远端 CI，未执行远端操作。
旧覆盖键和值格式不兼容，不实现 XPath 解析或列表差异合并。
整列表覆盖会替换来源的新列表，来源新增元素不会自动合入实例覆盖。
