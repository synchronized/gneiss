<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Core 版本与结果接口

## 版本

仓内使用 `gneiss::engine::library_version()` 返回运行时库版本，
`library_version_string()` 返回静态存储的版本字符串。声明位于
`src/engine/core/version.hpp`。构建版本头仅供内部实现与版本用例使用，不安装。
游戏模块协议已撤销，不再发布模块 ABI 版本。

## 错误状态与值结果

`gneiss::engine::error`（`engine/core/error.hpp`）保存一个 32 位状态码，提供
`ok()`、`failed()`、`code()`、`message()`，不提供布尔或隐式整数转换。
默认构造产生 unknown；非负值成功，负值失败。无返回值的操作直接返回 error。
`message()` 返回静态英文文本，未知数值返回 `unrecognized result`。
整数仅在协议等边界显式构造或通过 `code()` 读取，旧状态类型 result 不再保留。

`gneiss::engine::result<T, E = error>`（`engine/core/result.hpp`）互斥持有成功值或失败原因：

```cpp
auto loaded = resources.load<texture_resource>(uri);
if (!loaded)
  return loaded.error();
auto request = std::move(*loaded);
```

布尔为真表示有值；`*`、`->` 只能在成功时访问，`error()` 只能在失败时访问。
失败时不构造 T；允许仅移动、不可默认构造的值。结果本身不额外分配堆内存，
值或错误类型自身的分配行为不受影响。E 可使用带路径、位置与文本的解析诊断，
不要求每一个轻量 error 都携带动态字符串。`result<void>` 支持泛型代码，无值业务操作优先使用 error。

当前资源查询、创建、提交加载及内存解码入口使用值结果；底层缓冲区写入、就地编辑、
请求领取及其他领域既有接口仍按各自契约使用引用参数，不以本次调整宣称全引擎已消除 out 参数。
游戏模块与公共 C ABI 已撤销，不增加新的 C 转换层。

## 内部符号可见性

共享库的导入/导出宏定义在 `src/engine/core/export.hpp`，由受限内部头清单提供给仓内目标。
公共 include 目录和 C 结果码副本已删除，不再安装头文件；需要新的对外接口时另行设计。

## 内部基础身份类型

以下类型位于 `src/engine/core`，命名空间为 `gneiss::engine`，不属于公共 C ABI。
来源及许可见 [第三方声明](../../THIRD_PARTY_NOTICES.md)。

- `string_name`：驻留字符串，相同文本共享稳定条目；空串和空指针生成无效值。
  `find` 只查询，不创建；空串查询返回空 optional。比较与哈希使用条目地址，不能写入存档。
  Engine 内只有一张带锁字符串表，不逐条回收。`view()` 借用表内文本，所有引用必须在
  Engine 卸载前停止使用；构造与查询线程安全，可能抛出分配异常。
- `guid`：16 字节值，默认 nil。解析严格的 36 字符 UUID 文本，接受大小写十六进制，输出小写；
  不提供随机生成，也不替换当前场景 UUID 字符串。
- 稳定类型身份直接使用 `guid`，不再提供 `type_guid` 包装或兼容别名；不从地址或名称哈希推导。
- `core/traits` 下的 `type_name<T>` 与 `type_identity<T>`：名称与稳定 GUID 定制点。内置类型提供名称，默认 GUID
  为 nil；未定制用户类型名称为空。可显式特化 `type_identity<T>`；泛型调用方应先用
  `std::remove_cvref_t<T>` 归一化，再访问用户特化。

- `type_id`：Engine 内集中分配的运行期类型键。`runtime_type_of<T>()` 去除顶层 cv/ref，
  首次按工具链规范类型名注册，之后缓存编号；void 返回无效 ID。编译器类型名仅作为内部去重键，
  不提供按 ID 查询名称的接口，也不作为展示名称或持久化契约。
  资源缓存与 Loader 使用此键，不使用名称 hash 代替身份。首次初始化分配失败不可恢复。

`type_id` 不持久化，不替代反射的 16 字节 `guid`、稳定 GUID 或资产 UUID。
跨模块使用要求相同工具链/ABI、满足 ODR 的具名类型，并链接同一个 Shared Engine。
静态构建只保证同一链接映像内统一，匿名/局部类型不作为跨模块资源契约。
不提供类型擦除对象的析构回调或字段注册；具体资源仍由已有 `asset_ref` 与 Loader 管理。
见 [ADR-081](../decisions/ADR-081-runtime-type-identity.md)。
编译消费者应链接 Engine，不能把 string_name.cpp 各自复制进插件生成独立的字符串表。

## 数学类型与运算

`engine/core/math` 使用 `gneiss::engine` 命名空间，按职责组织，不依赖渲染后端：

| 文件 | 内容 |
| --- | --- |
| `types.hpp` | GLM 配置、向量/矩阵/四元数别名、分量和数组访问 |
| `functions.hpp` | 有限值判断、矩阵数组转换、位置与方向变换 |
| `transform.hpp/.cpp` | TRS 类型与校验、组合、Model/Normal 矩阵和法线变换 |
| `camera.hpp/.cpp` | camera_settings、视图矩阵和透视投影 |

`camera_settings` 字段为 `fov`（垂直视场角，弧度）、`near`（近裁剪面）和 `far`（远裁剪面）。
默认均为零，不代表有效投影；使用时要求 `0 < fov < π`、`0 < near < far`，且所有参数有限。
场景描述和文件/IPC 编码仍使用原有长字段名，边界转换显式映射这些字段。
`camera.hpp` 清除已定义的 Win32 near/far 旧宏；平台代码在其后包含 Windows 头时，
应在平台边界清除这两个宏，不能让宏影响后续成员访问。

数学函数以运算语义命名，不保留旧 build_* 或 valid_transform 转发入口：

| 接口 | 语义与失败行为 |
| --- | --- |
| `is_valid_transform` | 判断 TRS 是否满足既有有限值、单位旋转与非退化缩放约束 |
| `model_matrix` | 从有效 TRS 计算 Model 矩阵；不做输入校验 |
| `model_and_normal_matrices` | 计算 Model/Normal 矩阵；校验失败返回 false，保持输出 |
| `view_matrix_rh` | 右手视图矩阵，忽略缩放；失败返回 result，保持输出 |
| `perspective_rh_zo` | 右手、深度 [0,1] 透视投影；失败返回 result，保持输出 |
| `transform_vector(matrix4, vector3)` | 以 w=0 变换方向，不应用平移，不归一化或透视除法 |
| `transform_point(matrix4, vector3, output)` | 以 w=1 变换位置并透视除法；w=0、非有限结果或除法溢出时返回 false，保持输出 |
| `is_finite(matrix3/matrix4)` | 检查包括非对角项在内的全部分量 |

四维齐次坐标运算直接使用 `matrix * vector4`。编辑器 Gizmo 复用 Core 的相机函数，
在适配边界转换成外部数组；非法相机参数延续返回零数组的编辑器约定。
底层继续使用 GLM，不另外实现 add/subtract/multiply 或修改 normalize 的既有语义。
