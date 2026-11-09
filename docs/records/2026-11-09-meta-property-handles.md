<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 首批属性句柄迁移

## 范围

2026-11-09，开始实施 [PLAN-108](../plans/PLAN-108-meta-serialization-api.md)。
本批只迁移属性范围、句柄和遍历消费者，保留现有实例命名与序列化协议。

## 调用点审计

| 模块 | 基线与迁移方向 |
| --- | --- |
| meta/type.hpp、instance.hpp/.cpp | properties 原复制 vector<property_info>，改为保活注册表的只读范围 |
| meta/registry.cpp | 冻结已完成继承属性展开和排序；直接借用冻结数组，不维护第二份属性索引 |
| serialization/traversal.cpp | JSON 编码、校验、解码遍历接入句柄；保持只读/必填/枚举行为 |
| serialization/value.cpp | capture/apply 的字段枚举使用新范围，数据树格式不变 |
| editor/panels/reflected_property_editor.cpp、runtime_panels.cpp | Inspector 使用句柄取成员、查询属性能力 |
| editor/commands/author_property_edit.cpp、prefab_authoring.cpp | 属性路径校验和展示迁移范围查询 |
| function/scene/prefab_replay.cpp、prefab_property_override.cpp | 保留稳定路径与覆盖规则，迁移属性遍历 |
| host/runtime/runtime_property_editor.cpp | describe_type 信息查询仍待后续收敛 |
| serialization/binding.hpp、context.hpp、meta/registration.hpp | 尚未修改；后续依计划拆分策略、上下文和 builder |

当前 type_of 已存在，不重复增加。Ops 分类和 value 数据模型保持不变。
现有 meta-instance、反射 JSON、容器、序列化与编辑器回归作为迁移基线。

## 实现边界

范围、迭代器、属性句柄各自保活注册表，无注册表自持有循环；不保活目标对象。
属性句柄拒绝跨注册表使用以及相同 ID 的不相关类型访问；继承和只读行为复用实例访问。
空句柄返回 invalid_state；范围下标及迭代器采用标准范围前置条件。
旧信息结构仍供底层查询使用，实例访问仍有查找；本批未宣称所有查找和分配均消失。

## 验证

Windows Clang Debug 默认构建通过，最终增量日志为 build/plan108-build-final.log。
31 项定向 CPU 回归全部通过（58.95 秒），日志为 build/plan108-tests.log；覆盖 Meta、
序列化、结构化值、Scene/Prefab、编辑器属性编辑及核心/内部头边界。
新增测试覆盖跨注册表、相同 ID 不相关类型、继承基类调整、只读拒绝、空句柄及范围/迭代器/句柄保活。
格式检查与 git diff --check 通过。未执行 GPU、大场景、完整平台矩阵或远端 CI；未推送或发布。
首轮独立包含编译发现 property.hpp 缺少 result.hpp，已补显式依赖后验证通过。
