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
- include 仅暂留 export.h 可见性宏与 result.h 数值常量，不提供公共 C 操作函数。

## 验证

Windows Clang Debug 全部保留目标编译通过；干净前缀安装通过，模板不包含模块源码或构建预设。
格式、git diff --check、核心依赖边界和禁止公共 C 导出的静态检查通过。
本地编译日志：build/asset-migration/remove-module-complete.log；安装日志：scene-only-install.log。
功能测试、大场景、静态库与其他平台/CI 均未运行，不据此宣称行为验收完成。
