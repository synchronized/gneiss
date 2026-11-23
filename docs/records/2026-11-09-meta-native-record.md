<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 注册记录关联原生能力

模板类型与枚举注册将静态 Ops 关联到冻结记录；find_type 和 type_at 沿用同一能力。
纯运行期定义可保持空原生能力，注册时校验关联的类型 ID 与枚举承载类别。
没有引入查询时注册或可变全局能力缓存，冻结前取得的句柄仍保持原状态。

7 项重新构建后的 CPU 测试通过，覆盖类型注册、边界、builder、实例、分类 Ops、
序列化与嵌套 JSON。新增数组、映射、optional、ref、variant 的模板/ID 查询一致性、
固定长度和 type_at 能力、运行期无原生能力及冻结前句柄不升级测试。
日志为 build/plan108-native-record-build.log 和 build/plan108-native-record-tests.log。
git diff --check 通过；未执行 GPU、大场景和远端操作。实例布局及计划其余阶段尚待完成。
