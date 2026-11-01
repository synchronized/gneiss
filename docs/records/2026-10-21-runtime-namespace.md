<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Runtime 命名空间统一

日期：2026-10-21。依据：ADR-069 补充决策及模块布局计划。

## 交付

Application 对象从 gneiss::engine::application 移至 gneiss::runtime::application；
application_internal 合并为 gneiss::runtime，game_internal 改为 gneiss::runtime::game。
配置、场景域、加载事务、游戏上下文、C 适配及使用者同步更新，不保留旧别名。
原先依靠同命名空间查找的 action_map_request 显式标注 Engine 归属。
没有改变对象所有权、取消行为、公共 C 符号或共享值类型；内部 C++ 使用者须一起重编译。
边界检查拒绝恢复旧业务命名空间和旧 Application 类型引用，并增加对应负向夹具。

## 验证

- Windows Clang Debug 完整构建通过。
- 32 项定向回归通过，涵盖原生应用、场景/Prefab、编辑异步流程、ABI 与依赖边界。
- 无线程静态 CAORS Application 目标构建和单项测试通过。
- 更新安装包后，共享/静态 C、C++、Application/World、属性示例共 8 项 consumer 重链接及测试通过。
- clang-format 和 git diff --check 通过。

日志位于本地 build/runtime-namespace-*.log。未运行图形窗口、Linux/macOS/Emscripten 或远端 Actions。

## 尚未迁移

Engine 的 resource、function、core/platform 仍有旧业务命名空间，后续按模块迁移。
本批不宣称整个工程已统一，不改变 Editor 宿主拆分计划。
