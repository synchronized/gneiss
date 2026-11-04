<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-051：撤销动态 Game Module

状态：代码与编译收尾完成。已确认决策见 [ADR-082](../decisions/ADR-082-remove-dynamic-game-module.md)。

1. 删除 Runtime 模块装载、Context 与模块更新调度，保留场景运行与编辑 IPC。
2. 删除 Editor 模块构建后启动流程，工程打包只处理 Runtime 与资产。
3. 删除模块示例、协议、专用测试及安装消费者；普通功能用例去掉模块夹具依赖。
4. 清理工程描述、模板和文档，完成编译及静态边界检查并本地提交。

不设计客户 system 注册、脚本或新插件协议。只编译与静态检查，行为测试、大场景与 CI 延期。

## 完成结果

- 动态模块、Context、模块调度和 9 个 C 入口均删除，无模块示例 DLL 或替代注册框架。
- Editor 直接启动 Runtime；创建模板与发布包只包含场景、资产、Runtime 及其动态依赖。
- 旧 game_module 字段明确返回 unsupported，相关工程读取断言已补充但未运行。
- 模块专用用例删除；输入、场景切换、进程控制、资产工作流和打包用例保留并去除模块夹具。
- 公共 include 已删除；符号可见性宏移入 src/engine/core/export.hpp，C 结果码副本撤销。

## 验证

Windows Clang Debug 全部保留目标编译通过；干净前缀安装通过，模板不包含模块源码或构建预设。
格式、git diff --check、核心依赖边界和禁止公共 C 导出的静态检查通过。
本地编译日志：build/asset-migration/remove-module-complete.log；安装日志：scene-only-install.log。
功能测试、大场景、静态库与其他平台/CI 均未运行，不据此宣称行为验收完成。

## 残留清理

已删除 Runtime 无效的 --profile 参数和发布启动脚本传参；保留资产打包的
project_build_profile，包清单中的 debug/development/shipping 名称不变。
CMake 模板位置变量更名为 GNEISS_PROJECT_TEMPLATE_DIR。
固定更新计数从 Runtime、IPC 编解码和 Editor 展示中删除，统计往返用例核对全部保留字段。
无生产调用方的 game_updates_enabled 查询已删除；IPC 会话用例直接核对状态。
本批全部保留目标编译及静态边界检查通过，用例只编译不执行。

## 删除公共 include

已移除整个公共 include 目录和头文件安装声明。符号导出宏位于内部 Core，
结果类型只保留 engine::result；旧 C 结果头、公共头编译目标与类型清单检查均删除。
内部 C++ 头与功能用例保留。以后有明确对外需求时再设计公共接口。

Windows Clang Debug 全部保留目标编译通过；干净安装前缀无 include 目录，
导出 target 无公共头路径。格式、核心边界、公共接口残留及差异检查通过。
日志：build/asset-migration/remove-public-include-final.log、no-sdk-headers-install.log。
功能测试、大场景和 CI 未运行。
