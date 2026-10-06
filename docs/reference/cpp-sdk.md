<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# C++ SDK 的类型与借用边界

## 使用方式

`<gneiss/gneiss.hpp>` 汇总公共 C++20 SDK；也可以独立包含模块 `.hpp`。
包装通过 C ABI 操作同一个运行时，不创建第二套注册表、资源或主循环。
输入接口已使用原生 C++ 类型；其余描述、值类型及回调仍有 C 表达，正在按
[0.47 计划](../plans/VER-047-0.47.0-native-cpp-sdk.md)逐组迁移，尚不能称为全部原生 SDK。

| C 功能 | C++ 表达 | 所有权 |
| --- | --- | --- |
| Application、World、Scene Instance、Registry | `application`、`world`、`scene_instance`、`type_registry` | 不可复制、可移动的拥有者 |
| Mesh、Material、Texture | `mesh`、`material`、`texture`；对应 `*_id` | 拥有者与借用身份分开，材质不会隐式拥有纹理 |
| Prefab 刷新历史令牌 | `scene_prefab_refresh` | 拥有令牌；关闭不撤销当前投影，父 Scene/Application 失效后安全清空 |
| Entity、Scene Node、Action、Game Context、RID | `entity_id`、`scene_node_id`、`action_id`、`game_context`、`rid` | 值形式的借用身份，不延长所属服务寿命 |
| Application 所属 World | `world_ref` | 借用；不能调用拥有者的销毁/转移操作 |
| 结果、版本、日志级别 | `result`、`version`、`log_severity` | 值；结果不通过异常报告 |
| Transform、Render/Scene 描述 | 模块 `.hpp` 的值别名 | 与 C 布局相同；结构含指针不代表复制了指向的数据 |
| 输入事件、键盘/指针/动作快照 | 独立结构、强类型枚举与 variant | 自有快照，默认构造，详见[输入接口](input.md) |
| Application/World 创建描述、反射元数据、日志事件、Game Module 描述 | 直接使用 `gneiss_*` C 值 | 由对应操作决定复制与借用，见下表及模块参考 |
| 常量、标志、默认初始化器、旧结构大小、构建导出宏 | 输入业务常量使用 C++ 枚举/默认构造，其余仍使用 `GNEISS_*` | 无运行时所有权；初始化器保留 `struct_size` 与保留字段规则 |

拥有者的 `get()`/`id()` 只借用原始身份；`release()` 才转移销毁责任。
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

C++ 回调仍使用 C 函数指针与显式上下文；捕获状态需由调用方持有，不能注册临时对象后让其提前析构。
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
旧路径继续通过兼容头转发；详细规则见[公共头路径](../concepts/repository-layout.md#公共头路径)。
