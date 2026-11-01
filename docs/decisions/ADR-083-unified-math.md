<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-083：统一内部数学类型与运算

状态：已接受。

引入 GLM 1.0.1（锁定提交），选择 MIT 许可。Core 定义薄类型别名，不复制 Granit 数学实现，
不再创建另一套运算包装类。GLM 为纯数学值依赖，允许 ECS 数据组件使用；仍禁止后端资源对象进入组件。

约定：右手、列向量、列主序、投影深度 0..1、内部角度为弧度。四元数文件顺序保持 xyzw，
GLM 构造使用 wxyz，必须显式处理。零初始化和单位旋转/矩阵显式书写。
GLM 配置在统一头内固定；不启用对齐 SIMD 类型，不能把对象内存直接作为文件或 GPU 布局。

稳定反射 GUID 与文件格式不变；序列化、IPC、GPU、Granit 与 ImGuizmo 以字段或元素转换。
库函数处理普通运算，项目辅助仅定义合法性检查与 TRS/相机约定。
替代方案是复制 Granit，但会导致两份实现及补齐算法的维护成本；直接使用 Granit 会把 Core 绑定到后端。

2026-10-27 命名空间调整：`core/math` 中的数学别名、TRS、相机参数及辅助函数统一位于
`gneiss::engine::math`，避免向 Engine 平铺通用运算名称。不保留旧限定名兼容别名，
调用方、反射注册和通用 value 直接使用新名称。GUID、反射展示名、文件字段与数学约定不变。
头文件路径、GLM 配置和链接目标不变；`result`、`guid` 和 traits 定制点仍属于 Engine。
