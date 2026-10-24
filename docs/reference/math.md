<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 内部数学类型与边界转换

`engine/core/math/types.hpp` 是内部统一入口，必须先于其他 GLM 头包含。`gneiss::engine` 提供
`vector2`、`vector3`、`vector4`、`quaternion`、`matrix3`、`matrix4`，均为 GLM 的 packed float 别名。
不提供第二套数学对象包装，也不安装为公共 SDK。

## 运算与初始化

采用右手坐标、列向量、列主序、投影深度 0..1。矩阵访问为 `[列][行]`，内部角度使用弧度；
编辑器欧拉角界面使用度，在接口处转换。普通运算直接使用运算符及 GLM 函数。
`matrix4{1.0F}` 表示单位矩阵，`matrix4{0.0F}` 表示零矩阵；默认构造矩阵为单位矩阵。
默认向量为零，默认四元数为单位旋转。`transform` 默认平移为零、旋转为单位、缩放为一。

四元数构造函数参数为 **wxyz**。从资产文件的 **xyzw** 分量构造时使用 `quaternion_xyzw(x,y,z,w)`。
`components()` 提供向量和四元数的分量视图，四元数视图保持 xyzw；`matrix_elements()` 提供列主序矩阵视图。
视图不延长对象寿命，不得保存临时对象产生的视图。

`functions.hpp` 提供 TRS 检查、组合和矩阵转换。场景树保持分量 TRS 组合：父缩放、父旋转、父平移依次
作用于子平移，旋转相乘、缩放逐分量相乘。非均匀缩放与旋转产生的剪切不在此表示范围内。
Gizmo 保持正缩放、无剪切的限制。

## 反射与数据边界

`property_vec3` 和 `property_quaternion` 是统一类型的别名，现有反射属性种类及稳定 GUID 不变。
这次不增加 vector2、vector4 或矩阵的反射字段种类。

Scene、导入节点、运行时 Transform、渲染矩阵和编辑器计算使用统一数学类型。
Prefab 属性持久化载荷、IPC、网格/材质格式、GPU 数据和 ImGuizmo 接口仍保留各自的数组格式。
`component_array()`、`matrix_to_array()`、`matrix_from_array()` 或逐字段读写完成转换；
不把 GLM 对象直接 memcpy 为持久化格式或 GPU 结构，不因数学类型变更修改现有协议。

依赖选择与边界例外见 [ADR-083](../decisions/ADR-083-unified-math.md)，版本和许可证见
[第三方声明](../../THIRD_PARTY_NOTICES.md)。
