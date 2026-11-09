<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 序列化读写上下文拆分记录

日期：2026-11-09。关联 [PLAN-108](../plans/PLAN-108-meta-serialization-api.md)。

## 实施

- 删除通用 context，编码/捕获使用 write_context，解码/恢复使用 read_context。
- reference_resolver 仅由 read_context 借用；两个方向不能隐式互换。
- JSON、格式无关入口、特殊 codec、资产引用收集/绑定及资源格式包装同步迁移。
- 私有 context_base 复用注册表、限制和操作状态借用；嵌套入口保留路径、深度与环检测。
- 文件格式与 value 快照表示不变，不提供旧类型兼容别名。

## 验证

Windows Clang Debug：编辑器及全部本次修改的测试消费目标编译通过。
11 项定向 CPU 测试通过：serialization、serialization-archive、reflection-json、
meta-instance、meta-container、meta-type-ops、type_registry、type_builder、
component-description、scene_description、action_map。
静态断言覆盖不可复制、读写不互换、写上下文不接受解析器；
运行测试覆盖空/错注册表、递归 codec 深度限制与失败后复用。
渲染加载测试仅编译，未运行 GPU、大场景或远端 CI。

构建日志：build/plan108-direction-context-build.log、
build/plan108-direction-context-consumers-fix.log（本地生成，不纳入版本控制）。
