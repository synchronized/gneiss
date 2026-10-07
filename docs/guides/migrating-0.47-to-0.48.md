<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 从 0.47 迁移到 0.48 的 C++ SDK

## 适用范围

C 函数、结构布局、结果码和资源寿命保持不变。C++20 的规范头路径、类型名称和普通调用方式不变。
没有引入 expected、异常工厂或新运行时；C++ 消费者仍须重新编译。

## 安装与包含

继续包含 `gneiss/engine/<module>.hpp` 或 `gneiss/gneiss.hpp`。
复杂的转换、回调桥接和关闭操作移到自动包含的 `gneiss/engine/detail/*.inl`。
安装使用 CMake 安装目标并采用干净前缀；若自行复制 SDK，必须同时复制这些实现文件，不能只筛选 `.h/.hpp`。
不要直接包含 detail 文件或依赖其内部函数。

## const 与所有权

World 与 Scene 中原先不一致的操作统一允许通过 const 句柄包装调用。
const 只表示包装身份不变，不表示后端场景或实体只读；线程约束完全不变。
`reset`、`release`、移动等改变包装身份或所有权的操作仍不能通过 const 拥有者调用。

普通调用无需修改。显式保存成员函数指针时，检查其 const 限定，例如：

```cpp
using create_entity_fn = gneiss::result (gneiss::world_ref::*)(gneiss::entity_id&) const noexcept;
constexpr create_entity_fn create_entity = &gneiss::world_ref::create_entity;
```

涉及 World 的实体/节点创建销毁、相机及渲染组件设置，以及 Scene 的名称、层级、Prefab
刷新和删除操作。具体签名以模块头为准；不要把这项变化理解为线程安全能力增加。

## 错误与借用

继续显式检查 result；失败输出行为保持原约定，不把所有函数视作同一种失败策略。
例如 `get_active_camera` 失败清空输出，而 `get_local_transform` 的原生 C++ 输出在失败时保留。
创建实体不会自动建立场景节点，读写实体变换前先关联节点，见
[最小应用示例](application-world-example.md)。

[稳定运行时示例](../../examples/stable_runtime/main.cpp)展示 Application、Scene、输入动作与
相机变换的组合使用。场景在 Application 之前关闭，回调状态活到 Application 关闭完成。
