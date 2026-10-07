<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Application 与 World 的最小 C++ 示例

## 适用场景

从公共 C++20 SDK 开始：创建应用、借用 World、创建实体、修改变换、运行回调并退出。
示例不加载资产、不创建窗口，运行后输出 `updates=3, x=4`。

## 构建与运行

按[构建指南](building.md)配置后执行：

```sh
cmake --build build/windows-clang-debug --target gneiss_application_world_example
ctest --test-dir build/windows-clang-debug -R "^gneiss.application_world_example$" --output-on-failure
```

其他平台使用对应构建目录。目标需要 `GNEISS_BUILD_EXAMPLES=ON`，CTest 需要启用测试。

## 阅读顺序

1. [main.cpp](../../examples/application_world/main.cpp)：状态先于 Application 声明，保证
   `user_data` 活到关闭完成；创建后的 `world_ref` 不获得 World 所有权。
2. [scene_update.cpp](../../examples/application_world/scene_update.cpp)：从回调收到借用的
   Application，获取 World，读写实体变换，第三次更新请求退出。
3. 回到 main：检查回调次数及最终坐标，显式检查关闭结果。提前失败仍由 RAII 关闭应用。

每个可失败操作用 `if (const auto status = ...; status.failed())` 原样传播错误。
这是当前 SDK 的调用方式；示例没有用宏或隐式异常隐藏错误，也未引入新的结果类型。

示例每帧固定增加 X 坐标，目的是得到可复现结果；真实时间驱动动画应使用帧时间。
实体 ID 只表示身份，不自动带变换。必须先用 `create_scene_node` 关联场景节点，
然后才能通过实体读写变换，否则返回 `not_found`。实体和 World 由 Application 销毁；
不能把借用视图当作独立拥有者。

## 接口与实现

普通使用者只包含 `gneiss/engine/application.hpp` 和需要的模块头。
Application 与 World 的声明展示于模块头，适配定义由 `detail/*.inl` 自动包含。
实现文件必须随安装包存在，但不构成推荐使用入口。
具体错误、回调与所有权约束以 [C++ SDK 参考](../reference/cpp-sdk.md)为准。
