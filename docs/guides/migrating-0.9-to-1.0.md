<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 从 0.9.0 迁移到 1.0.0

## 适用范围

本文面向使用 Gneiss 公共 C11 或 C++20 SDK 的 0.9.0 调用方。0.x 没有二进制兼容承诺；升级时应
重新构建应用。仓库额外保留一个使用 0.9.0 Application v1 布局编译的 C Consumer，用于防止 Stable
候选运行时出现无意的 ABI 破坏，但它不把全部 0.9.0 接口追认为 Stable。

## 必须处理的源码变化

### 反射查询输出

Type Registry 与属性访问仍为 Experimental。`gneiss_type_info` 和 `gneiss_field_info` 新增首字段
`struct_size`；调用查询函数前必须使用初始化宏：

```c
gneiss_type_info type = GNEISS_TYPE_INFO_INIT;
gneiss_field_info field = GNEISS_FIELD_INFO_INIT;
```

未初始化或结构过小会返回 `GNEISS_ERROR_INVALID_ARGUMENT`。C++20 `gneiss::type_registry` 查询包装会
自动初始化输出结构。

### 定宽枚举类型

`gneiss_application_platform`、`gneiss_texture_format`、`gneiss_texture_color_space` 和
`gneiss_property_kind` 已改为 `uint32_t` 别名加固定值宏。使用公开 typedef 和常量的代码无需修改；
直接书写 `enum gneiss_application_platform` 等枚举标签的代码应改用对应 typedef。

### Experimental 标记

头文件使用空的 `GNEISS_EXPERIMENTAL` 宏标识仍可能演进的声明。它不表示弃用，也不改变调用约定。
逐符号分类见 [`abi/api-stability.txt`](../../abi/api-stability.txt)。

## 构建与运行环境

下游继续通过 `find_package(gneiss CONFIG REQUIRED)` 和 `gneiss::gneiss` 消费。启用 Granit 平台适配的
安装包还需要 Granit 0.4 的 Window、Input 和 RenderPipeline 组件。Windows Shared 构建运行时应
同时把 Gneiss 与 Granit 安装前缀的 `bin` 加入 `PATH`。

场景 v2、工程 v1、动作映射 v1、资产索引 v1 和 Mesh Binary v1 的格式版本没有因本次 ABI 加固而
改变，不需要批量重写资产。

## 建议升级步骤

1. 使用 1.0 SDK 重新配置并构建应用。
2. 修正直接使用 C 枚举标签的代码。
3. 为直接调用反射 C API 的输出结构补初始化宏。
4. 运行应用自身的场景加载、输入、资源和确定性退出测试。
5. Windows Shared 部署检查 Gneiss 与 Granit DLL 均可从运行时路径找到。
