<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 最终验收记录

日期：2026-11-09。关联 [PLAN-108](../plans/PLAN-108-meta-serialization-api.md)、
[ADR-113](../decisions/ADR-113-meta-serialization-boundaries.md)。

## 交付

- type/property/instance 门面、原生能力与注册记录分离、属性局部 builder 已完成。
- content/reference/field 策略及领域校验边界已迁移；组件直接更新采用前也显式校验。
- read_context/write_context 与操作状态分离，通用及 JSON 入口统一，旧 context 删除。
- 快照、资产引用、场景/Prefab、IPC、编辑器和示例使用新接口；不改变文件信封及引用语义。
- 五类容器视图移动到 meta/views，无旧路径转发头。
- 内置类型和数学注册移动到 Serialization，入口为 serialization::register_builtin_types。
  Meta 只提供注册和扩展存储，不反向依赖具体持久化策略；应用初始化统一调用组合注册。
- 扩大整个 Meta 目录的依赖检查，补充内置注册与视图目录反例。
- 修复属性检查器示例残留的 result 风格属性范围访问，更新当前 Reference 与版本计划状态。

## 执行结果

Windows Clang Debug，共享引擎、编辑器、相关 Runtime/IPC、导入 SDK 和资产工具目标编译通过。
最终选中的 CPU 测试均重新构建后执行：44 项核心/场景/资产/IPC/编辑历史测试、
2 项正反依赖边界检查、1 项材质测试，共 47 项通过。
覆盖旧记录中的关键语义：只读与 getter 边界、继承地址、独立注册表、类型/属性查询、
可选/容器/Variant/多态、引用环和深度、格式往返、资产弱缓存、Prefab 覆盖、文档撤销快照。

使用 compile_commands 中实例测试的 C++20 编译选项，逐一包含 meta 根目录、ops、views
以及 serialization 根目录的 34 个头文件，启用原有警告和 -Werror，独立语法编译全部通过。
无旧 view 路径、旧 context、JSON serialize/deserialize、property_metadata、
describe_type/describe_property 或 mutable_instance 的生产/测试/示例代码引用。
git diff --check 通过。

本地生成日志（不纳入版本控制）：

- build/plan108-final-layout-build.log：编辑器和核心目标。
- build/plan108-final-cpu-build.log：最终 CPU 测试目标。
- build/plan108-final-cpu-tests.log：44 项回归执行。
- build/plan108-final-material-build.log：材质消费目标。
- build/plan108-header-check.log：34 个独立头文件。

边界检查通过 ctest 的 gneiss.core-boundary 与 gneiss.core-boundary-negative 执行；
材质通过 gneiss.assets.material 执行。

## 未验证范围与保留限制

未运行 GPU、大场景、远端 CI、MSVC/GCC/Linux/Web 或完整静态构建矩阵；
这些不能据本次 Windows Clang 结果宣称通过，仍归版本验收。
Binary 后端、插件卸载、未知类型保留及任意对象图别名持久化属于非目标。
借用视图仍要求宿主对象存活，容器结构修改后重取元素视图；context 不并发共用。
PLAN-109 异常策略未在本计划顺带实施，DEV-052 版本未推送或发布。
