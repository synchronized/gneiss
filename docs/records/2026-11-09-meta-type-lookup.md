<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 类型查询返回形式收敛

## 变更

type_of 返回原生类型句柄；find_type 返回可空类型句柄，不再用 result 表示未找到。
序列化策略入口和 from_object 保留注册表有效性与冻结检查，操作错误仍明确返回。
编辑器、运行时属性编辑、组件适配和序列化调用统一使用新接口。
type_at 的索引与注册表错误行为保持不变。

## 验证

编辑器及相关目标编译通过；重新构建后的 8 项 CPU 测试通过，包括类型注册、注册边界、
实例、builder、序列化、组件反射、材质编辑和运行时属性编辑。
日志：build/plan108-type-lookup-final-build.log、build/plan108-type-lookup-final-tests.log。
git diff --check 通过。未执行 GPU、大场景和远端操作。
属性列表及实例布局仍待后续阶段完成。
