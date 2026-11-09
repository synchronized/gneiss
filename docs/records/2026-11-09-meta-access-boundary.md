<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 视图访问边界

## 范围

2026-11-09，完成 [PLAN-108](../plans/PLAN-108-meta-serialization-api.md) 第 4 阶段访问边界清理。
实例公开所属注册表和受约束属性的候选替换操作；引用视图公开实际类型、只读强引用保活和
显式类型擦除赋值。序列化、JSON、value 快照统一使用这些正式接口。
删除 property_view_access.hpp 和 reference_view_access.hpp，移除相关构建清单项。
普通消费代码不再直接包含 Meta detail 头；模板内部仍保留必要实现依赖。

## 验证

默认 Windows Clang Debug 构建通过，日志 build/plan108-access-build.log。
首次配置发现内部头清单仍引用删除的文件，修复清单后完整构建通过。
40 项定向 CPU 测试全部通过，日志 build/plan108-access-tests.log。
新增引用视图用例检查保活、清空后重新赋值、类型 ID 不匹配拒绝且不修改槽位；
既有回归覆盖受约束 setter 替换、引用环、只读视图、对象快照和编辑器调用。
clang-format 差异检查及 git diff --check 通过。
未执行 GPU、大场景、跨平台矩阵或远端 CI。

## 后续

第 5 阶段将移除 Meta 对具体序列化 binding 的依赖，按计划拆分策略和扩展元数据。
第 6 至 8 阶段仍待完成，本记录不表示整个计划完成。
