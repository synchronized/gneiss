<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 从 0.46 迁移到 0.47

0.47 保持 C 函数与结构布局，调整公共头路径和 C++ 使用方式。C++ 消费方需要修改源码并重新编译。
完整接口契约见 [C++ SDK 参考](../reference/cpp-sdk.md)。

## 头文件与安装

| 旧包含路径 | 新包含路径 |
| --- | --- |
| `gneiss/application.h` / `.hpp` 等功能头 | `gneiss/engine/application.h` / `.hpp` |
| `gneiss/core/result.h` / `.hpp` 等 Core 头 | `gneiss/engine/core/result.h` / `.hpp` |
| `gneiss/gneiss.h`、`gneiss/gneiss.hpp` | 保持不变 |

26 个转发头已经删除。安装到新的空 SDK 前缀，再让消费者重新配置 CMake；直接覆盖旧前缀不会删除
残留旧头，不能用它判断迁移是否完成。CMake 包名和目标 `gneiss::engine` 保持不变。
不要为了保留旧代码再复制转发头；安装验证会逐一拒绝旧路径仍可编译的前缀。

## 原生值与描述

普通 `.hpp` 接口使用 `gneiss::` 自有类型，不再接受旧 C 描述或初始化宏：

```cpp
gneiss::application_desc desc{};
desc.asset_root = "assets";
gneiss::application app;
const auto created = gneiss::application::create(desc, app);
if (created.failed()) {
  return 1;
}
gneiss::transform pose{}; // 默认单位变换
pose.translation = {1.0F, 2.0F, 3.0F};
```

- 描述不再填写 `struct_size`、保留字段和手工数组计数。文本用 string_view，数组用 span/array。
- 业务常量改用原生枚举，例如 `texture_format::rgba8_unorm`、`material_alpha_mode::opaque`。
- 资源字段用 `mesh_id` / `material_id` / `texture_id`，拥有者通过 `id()` 提供借用身份。
- 输入事件及属性值使用 variant，标签由活动值推导；Reflection 字段 ID 使用 `field_id`。
- 头版本用 `header_version` / `header_version_string`，运行时版本用 `library_version()`。
- 显式 C 互操作使用 `to_native` / `from_native` 或带 `_native` 的入口，不强转独立 C/C++ 类型的指针。

Render 的元素数组转换可能临时分配，失败通过 result 返回；像素和索引只在同步调用中借用。
Reflection 的 type_info 自己拥有字段数组，名称仍借用 Registry，复制输出不延长名称寿命。

## 回调与所有权转移

Application 配置中的回调移至 `desc.callbacks`，参数使用 `application_ref`、`frame_time` 等原生类型，
返回 `result`。Application、Reflection 和 Game Module 的原生回调都必须声明 noexcept；
需要恢复的异常应在业务回调内捕获并转为结果，不能让其逃出回调。

Application 和 Registry 的 `release()` 分别返回 `released_application`、`released_type_registry`。
载体同时拥有句柄和回调适配存储，不能再把返回值当裸整数保存或抛弃：

```cpp
auto transferred = app.release();
gneiss::application destination;
const auto adopted = gneiss::application::adopt(std::move(transferred), destination);
```

接管失败保留源与目标；载体析构会关闭对象。`get()` 仍只借用句柄，不转移所有权。
调用方的 user_data 仍须存活至关闭完成，不能因包装复制了函数表而提前释放业务上下文。
World、Scene 和资源的转移规则见各模块参考，不机械套用 Application 的释放载体。

## 游戏模块

模板使用 `game_module<Callbacks>` 和原生生命周期回调，自己的状态仍由 initialize / shutdown 管理。
真实导出函数 `gneiss_game_module_query` 保留 C 签名及导出宏，内部调用 `export_query`。
模板示例见 [game_module.cpp](../../templates/game/game_module.cpp)。
模块继续要求共享 Engine SDK，不因原生 C++ 包装而支持独立静态注册状态互通。

## 验证顺序

1. 更新包含路径、原生描述、枚举及回调签名，使用项目原有警告等级重新编译。
2. 核对所有 release 接收方、回调上下文寿命、查询文本借用及失败输出处理。
3. 用干净安装前缀构建消费者，并按原来方式运行游戏模块和场景工作流。

本版没有插件 API 或新的公共 Editor API，也没有升级 Granit。
