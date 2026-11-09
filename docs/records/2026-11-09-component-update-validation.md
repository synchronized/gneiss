<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 组件编辑采用校验记录

日期：2026-11-09。关联 [PLAN-108](../plans/PLAN-108-meta-serialization-api.md)。

组件领域规则移出通用序列化后，component_descriptions::update 必须独立校验候选。
本次在恢复并隔离候选之后、替换集合之前调用 validate_component_description；
失败保留原引用和内容。该声明归组件描述接口，实现继续在场景领域校验模块。
通用快照仍只负责结构读写，set/replace 的装配行为未改变。

Windows Clang Debug 构建通过，component-description、editor.component-edit、
editor.author-property-edit 三项 CPU 测试通过。
新增非法相机范围与 nil UUID 的更新拒绝用例，验证原引用不变。
未运行 GPU、大场景或远端 CI。
