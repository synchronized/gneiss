<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# C++ SDK 的类型与借用边界

## 使用方式

`<gneiss/gneiss.hpp>` 汇总公共 C++20 SDK；也可以独立包含模块 `.hpp`。
包装通过 C ABI 操作同一个运行时，不创建第二套注册表、资源或主循环。
Application、World、Scene、Render、Input、Reflection 和日志接口已使用原生 C++ 类型；Game Module 导出边界仍在按
[0.47 计划](../plans/VER-047-0.47.0-native-cpp-sdk.md)逐组迁移，尚不能称为全部原生 SDK。

| C 功能 | C++ 表达 | 所有权 |
| --- | --- | --- |
| Application、World、Scene Instance、Registry | `application`、`world`、`scene_instance`、`type_registry` | 不可复制、可移动的拥有者 |
| Mesh、Material、Texture | `mesh`、`material`、`texture`；对应 `*_id` | 拥有者与借用身份分开，材质不会隐式拥有纹理 |
| Prefab 刷新历史令牌 | `scene_prefab_refresh` | 拥有令牌；关闭不撤销当前投影，父 Scene/Application 失效后安全清空 |
| Entity、Scene Node、Action、Game Context、RID | `entity_id`、`scene_node_id`、`action_id`、`game_context`、`rid` | 值形式的借用身份，不延长所属服务寿命 |
| Application 配置与回调 | `application_desc`、`application_callbacks`、`frame_time`、`diagnostic`、`log_event` | 配置文本借用至 create 返回，回调表复制，user_data 与回调文本的寿命见 Application 参考 |
| Application 所属 World | `world_ref` | 借用；不能调用拥有者的销毁/转移操作 |
| 结果、版本、日志级别 | `result`、`version`、`log_severity` | 值；结果不通过异常报告 |
| Transform、Camera、MeshRenderer | 独立 C++ 值、数组与资源 ID | 逐字段适配 C ABI，不共享类型布局 |
| Render 描述 | 原生枚举、资源 ID、数组值与 span | 输入借用至调用返回；属性转换临时分配，C ABI 内部再复制到资源 |
| Scene 描述与查询 | 原生 ID、变换、标志枚举与 string_view | 输入借用至调用返回；查询文本借用至下次修改/父对象失效 |
| 输入事件、键盘/指针/动作快照 | 独立结构、强类型枚举与 variant | 自有快照，默认构造，详见[输入接口](input.md) |
| 日志提交消息 | `log_message`，字段使用 `log_severity`、`string_view`、`result` | 借用文本至同步提交返回，详见[日志契约](logging.md#c-日志提交) |
| 反射元数据与属性 | 原生描述、字段 ID、variant 与 noexcept 回调 | type_info 拥有字段数组，名称和属性字符串按各自约定借用 |
| World 创建描述与 Game Module 导出 | 无字段配置由 create 内部填入；模块导出仍遵循 C ABI | Game Module 边界审计尚在进行 |
| 常量、标志、默认初始化器、旧结构大小、构建导出宏 | 输入业务常量使用 C++ 枚举/默认构造，其余仍使用 `GNEISS_*` | 无运行时所有权；初始化器保留 `struct_size` 与保留字段规则 |

拥有者的 `get()`/`id()` 只借用原始身份；`release()` 才转移销毁责任。Application 返回
携带回调存储的 `released_application` 载体；Registry 返回 `released_type_registry`，其余拥有者按各自契约转移原始身份。
`operator bool` 或 `is_valid()` 只判断非零，不探测后端是否仍然存在。
关闭、移动覆盖与父对象先销毁规则以 [Application](application.md#生命周期)、
[Render](render.md)、[Scene](scene-instance.md) 和 [Reflection](reflection.md) 为准。

## 指针、字符串与回调

| 数据 | 有效期与复制行为 |
| --- | --- |
| Application 描述内回调及 `user_data` | 调用方持有到 Application 关闭返回；生命周期回调在创建线程执行，日志在消费线程串行执行 |
| Application 帧时间、诊断、日志事件 | 仅当次回调期间借用；不能保存内部字符串指针 |
| Mesh/Texture 创建数组、UI/Debug 提交数组 | 成功调用返回前复制；其中引用的资源 ID 仍是借用 |
| Scene 查询节点描述中的字符串 | 场景修改/刷新/卸载或父对象销毁后须重新查询；长期保存应先复制 |
| Registry 注册的名称、字段描述 | 注册时复制；冻结后查询的元数据借用至 Registry 销毁 |
| 属性 getter/setter 与 `user_data` | 调用方持有至 Registry 销毁；不存在单独注销 API，关闭前停止并发访问 |
| 属性字符串返回值 | 由访问器定义寿命；setter 输入只在调用期间有效；需要保存时复制 |
| Game Module 描述与函数指针 | 动态库卸载前保持有效；模块私有状态由模块初始化/关闭回调负责 |
| Game Context、启动实体、输入动作 | 不拥有 Engine 对象；关闭或场景切换后重新获取，动作映射重载使旧动作失效 |

Application 原生回调使用 noexcept C++ 函数指针和显式上下文，其函数表被复制；其他尚未迁移的
回调及 create_native 使用 C 函数指针。捕获状态需由调用方持有，不能注册临时对象后让其提前析构。
包装不提供隐藏的 `std::function` 注册表，也不因保存一个回调而延长 DLL 或 Application 寿命。
所有回调实现均不得让异常穿过 ABI；日志 Sink 和反射访问器有防御性异常隔离，但不能把这一点
泛化为全部生命周期回调均可抛异常。模块更新与 Application 关闭等回调应自行捕获并按其签名报告错误。

公共 C ABI 当前没有异步请求拥有者或取消令牌，因此 SDK 不承诺析构等待任务、后台取消或自动注销。
内部任务/资产服务的生命周期不因此成为公共 API。

## 审查依据

当前版本的[函数清单](../../abi/cpp-api-inventory.json)记录 C++ 入口与测试；
[类型清单](../../abi/cpp-type-inventory.json)枚举公共类型（含回调）和宏，包括生成版本头模板。
头保护宏不计入，平台条件下重复定义的导出宏只登记一次。`native_review` 独立记录原生迁移的
`pending`、`implemented` 和类型/常量的 `abi_only` 例外；旧 `reviewed` 不代表原生迁移已完成。
检查器的 `GNEISS_REQUIRE_NATIVE_CPP_SDK=ON` 严格模式拒绝 pending，普通模式允许显式待办。
清单检查验证新增/遗漏和直接 C 别名误标，不能代替签名、生命周期与消费者语义审查。
具体错误输出、所属线程和失效行为以模块 Reference 为准。

公共功能头的规范路径为 `<gneiss/engine/模块.hpp>`，根总入口保持 `<gneiss/gneiss.hpp>`。
旧功能头与 `gneiss/core/` 转发路径已删除；请使用干净安装前缀，详细规则见[公共头路径](../concepts/repository-layout.md#公共头路径)。

### Transform 值

`gneiss::transform{}` 是独立 C++ 值，三个字段分别为 `std::array<float, 3>` 平移、
`std::array<float, 4>` 四元数和 `std::array<float, 3>` 缩放；默认单位变换，无需 C 初始化宏。
World 节点和实体的变换读写、Prefab 来源变换设置使用此类型；失败查询保留调用方输出。
显式 C 互操作使用 `to_native` / `from_native` 逐字段复制，不能将两种类型的指针互相转换。

### Camera 与 MeshRenderer 值

`camera_desc{}` 与 `camera{}` 提供原生默认视角、近远裁剪面；后者的 `is_primary` 是 bool。
`world_ref::get_camera` 在失败时保留输出；线程限制仍与 C API 一致。
`mesh_renderer` 的 `mesh` 与 `material` 分别为 `mesh_id` 和 `material_id`，仅借用资源，
可用资源拥有者的 `id()` 填入，不延长父 Application 或资源寿命。需要 C ABI 值时显式调用
`to_native`；从 C 值构造则使用 `from_native`。

### Render 描述与临时数组

`mesh_desc` 的顶点、法线、切线、第二组 UV、颜色和索引使用 `span`；`texture_desc::pixels`
使用字节 span。`material_desc` 的 Alpha 模式、采样模式是枚举，纹理是 `texture_id`，
双面开关为 bool。各描述默认构造，无需填写 C 布局大小或数组计数。

网格属性、UI 顶点/命令和调试线段在包装内逐元素转换为临时 C 数组；像素与索引直接借用。
转换数组和输入借用都只持续至同步调用返回，成功后由底层复制到资源或当前帧。
这一步有临时分配成本；内存不足返回 `out_of_memory`，计数超过 ABI 表达范围返回
`invalid_argument`，资源拥有者和输出 ID 在失败时保持原值。包装不强转两种元素的指针。
显式 `to_native(texture_desc)` 仍借用原像素，不能超过像素存储寿命。

`application_ref::submit_ui_draw_list` / `submit_debug_draw_list` 只允许 update 回调内调用；
原生描述不会改变所属线程、同帧替换或帧结束失效的规则。

### Scene 借用与查询

节点/Prefab 创建描述使用 `string_view` 和原生 Transform，输出元数据直接返回原生 ID、标志和文本视图。
查询失败保留输出；成功查询的文本由场景拥有，只借用到下次场景修改或父对象失效，跨修改必须先复制。
`restore_subtree` 接收 `span<const scene_uuid_mapping>`，临时转换映射数组，失败保留根 ID 输出。

普通 Prefab 刷新使用 `scene_prefab_refresh` RAII 令牌；裸 C 令牌互操作分别使用
`refresh_prefab_instance_native`、`toggle_prefab_refresh_native`、`release_prefab_refresh_native`。
这些显式入口不会接管裸令牌的销毁责任，不能替代普通拥有者。

### Reflection 的值与生命周期

原生 `type_id`、`field_id`、`field_desc`、`type_desc` 描述注册信息；注册复制文本和字段。
`type_info` 拥有转换后的字段数组，名称仍借用冻结 Registry 的文本；复制/移动输出不产生临时数组悬空。
`property_value::payload` 是 variant，`kind()` 由活动类型确定；无独立标签需要手工同步。
字符串使用 string_view，寿命由访问器约定。普通访问器使用 noexcept 的原生参数，
`bind_property` 复制回调表，user_data 必须存活到关闭。成功 getter 返回不可表示的值按适配器错误返回 internal。

Registry 的移动、release/adopt 同时转移句柄与回调存储，release 返回 RAII 载体；不能把载体隐式转为整数。
冻结前注册/绑定及关闭需要调用方遵守外部同步契约；包装不提供注销，不复制第二套元数据注册表。
显式 C ABI 互操作方法使用 `_native` 后缀；Editor 现有 C 属性值边界及 C 回调异常夹具使用这些入口。
新示例使用普通原生方法，见 `examples/property_inspector/main.cpp`。
