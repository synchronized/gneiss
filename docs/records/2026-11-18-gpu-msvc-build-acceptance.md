<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 完整 GPU 套件与 MSVC Release 构建验收

## 范围

本轮承接 PLAN-119 收尾：验证当前 GPU 集合、MSVC Release 和真实消费者，核查旧目录检查与当前文档。
不扩展 PLAN-118 的错误语义迁移，不执行远端操作。

## 已完成检查

- Windows Clang Debug 当前 GPU 标签集合：20/20 通过，串行运行耗时 89.81 秒。
  包含双视图、Lantern、Runtime/Editor 窗口流程、资产像素与负缩放。
  此结果取得于本轮 MSVC 兼容性修正之前，不能替代后续修正的消费者构建。
- 公共接口边界检查此前仍扫描已删除的 src/engine/api、src/runtime/api，可能空扫通过。
  改为当前 Engine/Runtime 源码目录，并分别注入禁止 C API 的反例；2/2 检查通过。
- Guide/Reference 中当前模块源码位置及模板目录说明已校正，历史 Record 未改写。

## MSVC 发现与修正

使用 windows-vs2022-release，开启 Editor、Runtime 和测试，保持警告视为错误。
MSVC 19.41.34123，Windows SDK 10.0.22621.0，Granit 0.71.0。

- 启用自有 C++ 目标的 /Zc:preprocessor，修复断言包装的可变参数展开。
- Logging 和 Material 从整类 DLL 导出改为非内联方法导出，避免导出 STL 数据成员；
  共享所有权及日志默认路由不变。
- 消除 Meta/Render/Asset 局部变量遮蔽及模板实例化后的不可达分支。
- World 析构使用函数体内异常边界，保留不可恢复错误的 Fatal 策略。
- 内置组件模板的显式实例化补上导出标记，修复 MSVC 消费者的链接缺口。
- 修正资产模板未使用参数、编辑器及测试局部遮蔽与测试浮点构造实参，不改变断言覆盖。
- codec 模板函数指针采用显式 bool 转换的 if constexpr，保留空函数指针的注册失败测试，
  同时满足 MSVC 常量条件与 Clang 空指针转换诊断。

Clang 重编译首次在 Lantern 资产目录重命名时报告拒绝访问；未修改断言或资产流程，
再次构建该步骤成功。尚未确定首次目录访问失败的原因。

## 后续 Clang 验证

- 兼容性修正后完整构建通过；资产像素和负缩放复测 2/2 通过。
- CPU 全集首次 212/213 通过，editor.prefab-edit 超时；停止编译后独立复测通过，
  用时 27.26 秒。没有修改超时或断言，首次超时根因尚未确定。
- value 校验模板最后一轮修正后再次构建通过，Value/Meta 等相关测试 14/14 通过。

## 验收边界

最终 Windows Clang Debug、MSVC Release 完整构建均通过，包含 Editor/Runtime、示例和测试消费者。
MSVC Release CPU 全集 213/213 通过（35.23 秒）；最终 Clang 相关定向回归 29/29 通过。
MSVC 本地构建使用 CL=/MP2 与 CMake --parallel 2 限制并发，不改变仓库编译选项；/Zc:preprocessor
由自有目标统一设置。MSVC Release GPU 全集 20/20 通过（57.09 秒），合计 233/233 通过。
已有 GPU 结果不包含专门的压力、设备丢失或预算测量。
Linux、Web、MSVC Static 及远端矩阵未执行。
