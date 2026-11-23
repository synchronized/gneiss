<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Meta 分类视图整理

2026-11-04，分支 `feature/0.52-architecture-cleanup`。本轮承接操作表拆分，不代表版本整体完成。

## 交付

- 将 container_view 拆为 basic_sequence_view、basic_associative_view、basic_optional_view，分别保留只读/可写别名。
- 新增 reference_view / mutable_reference_view；通过 instance::as_reference 获取。
- 引用视图提供 has_value/get/as_const，修改入口为 assign/reset；不隐式保活目标，不改变 const 目标约束。
- optional 不再用容器 size/at 表达访问；序列和映射保持既有 API。
- 删除 instance 的 dereference、reference_owner、reset_reference、assign_reference；对象图编解码的保活和回填集中在内部辅助接口。
- 更新 CMake 头文件清单、调用方与接口参考；四类头可以独立包含，instance.hpp 继续汇集视图。

## 验证

直接相关的 Clang Debug 四项测试通过，包含新增的空引用、共享赋值、类型不匹配及重置检查。
Windows Clang Debug 全量编译及 24 项相关测试通过（含 Meta、JSON、场景/Prefab、编辑器和边界检查）。
四个视图头分别以独立翻译单元进行 Clang C++20 语法编译，通过。
Windows MSVC Release 四个相关目标构建及四项测试通过。
git diff --check 通过；src/tests 不再使用旧引用入口或 container_view 模板。

开发中深层 JSON 测试曾触发 Debug 栈溢出；将新增引用视图临时对象移出递归序列化函数，
改由非递归内部辅助函数处理后复验通过，未修改深度限制或测试要求。

视图仍为借用：容器结构修改、引用重绑或父对象销毁后，调用方须遵守原有失效规则。
未运行远端 CI、Linux/GCC、Sanitizer 或大场景/GPU 工作流。
