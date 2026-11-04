<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Resource 分类与 Runtime 目录迁移验收

## 范围

按 [模块布局计划](../plans/PLAN-051-module-layout.md) 完成资源内部分类及 Application 迁移。
当前路径权威来源为[源码目录与所有权](../concepts/repository-layout.md)，边界决策见
[ADR-069](../decisions/ADR-069-resource-runtime-boundaries.md)。

- Resource 分为 types、loaders、vfs、cache、backend/caors；管理器与请求契约保留根部。
- Application 及场景加载流程迁至 runtime；配置归 configuration，加载与采用归 loading。
- 游戏上下文归 runtime/game，依赖 Application 的 C 适配归 runtime/api。
- 删除 function/application 与 function/game，不保留转发头。
- 更新构建、私有头清单、测试与示例引用；增加 Engine 到 Runtime 的反向依赖负向检查。

这次只调整文件归属和边界验证，不改变对象所有权、公共符号、加载或回滚行为。
混合提供资源及 Application 接口的 Scene/Render 适配文件整体归 runtime/api，继续使用
engine/api 的值转换辅助。仍编入既有 gneiss_engine，不新增库或跨 DLL 边界。

## 验证

2026-10-21，本地 Windows Clang：

- 完整 windows-clang-debug 构建通过，包含 Editor、Runtime、工具、示例和测试目标。
- 36 项相关 CTest 通过：C/C++ Application、Scene、Game、输入、CAORS、场景加载、
  Prefab 准备及重载、纹理加载、编辑命令、私有头可见性与边界正负向测试。
- sc-mat-s 静态无线程配置的 gneiss_caors_application_test 构建及测试通过。
- git diff --check 通过。

日志：build/runtime-layout-build.log、build/runtime-layout-tests.log、
build/runtime-layout-static.log。构建目录不提交。

## 尚未验证与剩余工作

本次未运行 GUI 手动验收、完整 CTest、Linux/Web 与安装 Consumer 矩阵。
资源缓存统一和场景 session/state/service 对象收敛尚未完成；目录迁移不表示版本整体完成。
