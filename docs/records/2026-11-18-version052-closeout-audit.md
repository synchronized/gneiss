<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 0.52 收尾审计（2026-11-18）

## 范围与方法

在 feature/0.52-architecture-cleanup 对照计划、当前头文件、模块 CMake、测试枚举及历史
验收记录；本轮不新建重构计划、不触发远端操作。跨计划执行清单唯一维护在
[DEV-052](../plans/DEV-052-architecture-cleanup.md#收尾审计后的明确待办)。
历史通过仅证明当时提交；未逐项复验的内容不写成本轮运行结果。

## 计划状态核对

| 范围 | 判断与证据 |
| --- | --- |
| PLAN-108/109/110/113 | 子计划已有最终本地验收，旧版本文档仍写待实施；本轮不重新迁移 |
| PLAN-116/117 | 日志与命名空间已有实现和验收，路线图落后；修正文档状态 |
| PLAN-120 | 八域 IPC 反射与边界本地验收完成；stdout 与空负载消息保留有明确理由 |
| PLAN-118 | P0/P1 完成；Meta getter/ops、Render 回执与 Prefab 重放审计未完成。instance/registry 仍有错误返回，但错误码本身不是应删除的证据 |
| PLAN-119 | engine/tooling 等 include/src、目标和领域测试布局已落地；标签、白盒依赖闭包、完整矩阵仍需收尾 |
| PLAN-114 | 首轮有限测试清理已有记录；剩余辅助/标签工作由 PLAN-119 承接，避免两份执行队列 |
| PLAN-115 | 生产 World binding 与命令队列、双视图已接入；历史双视图回读不等于当前压力、回收、设备失败与预算测量完成 |

## 源码事实与保留接口

- engine/include/gneiss/engine/function/application/application.hpp 提供 load_assets，旧
  request_assets/poll_scene/poll_assets 不再出现。world.hpp/game_object.hpp/scene_instance.hpp
  为对象入口；旧 entity_create/node_create/entity_get_local_transform 不再出现。
- capture_frame 仍是输出参数。request_shutdown/advance_shutdown 和同步 shutdown 共存；
  application.cpp 析构调用 shutdown。环境读取仍由 Application 拥有专用 environment_read。
  这些是待评估的真实接口，不直接认定为错误或要求删除。
- tooling/CMakeLists.txt 直接依赖 gneiss::engine，include 为构建树契约，src 为私有实现。
  engine/CMakeLists.txt 独占生成版本头；安装规则没有安装原生公共头，Guide 的 SDK 表述过时。
- 共享配置当前枚举 233 项：15 项带 cpu，7 项带 gpu，1 项带 death，211 项无标签；
  标签可重叠。未分类不等于 CPU 安全，不能用排除 gpu 宣称全量 CPU 验收。

## 本轮发现与修正

静态完整构建在 cpp_version_test 暴露过时 include：测试仍使用
src/generated/include 与 engine/core/build_version.h，实际生成位置为
engine/generated/include/gneiss/engine/core/build_version.h。共享旧构建树残留曾掩盖问题。
修正测试 include 和私有生成头搜索路径，不增加兼容头或扩大 Engine 的导出使用要求。

首次共享安装发现 libassert/cpptrace/spdlog 许可证目标变成 /doc/gneiss/licenses。
根 CMake 在依赖定义安装规则之后才加载 GNUInstallDirs，目录变量当时为空；本轮将其
初始化前移至 project 之后。最终安装启用 CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION，
验证自有安装规则遵守所选前缀，不以补偿性复制替代修正。

路线图、VER-052、DEV-052 及相关计划状态改为链接统一清单。构建与代码指南修正当前
模块 include/src、没有安装头文件 SDK、Granit 不再是可选平台开关等过时表述。

## 验证结果

本轮使用 Windows Clang Debug，日志位于本地 build/audit-*.log，不提交构建产物。

| 验证 | 结果与边界 |
| --- | --- |
| windows-clang-debug 完整构建 | 通过 |
| windows-clang-static-debug 完整构建 | 显式启用 GNEISS_BUILD_EDITOR/Runtime 后通过，包含真实 Runtime/Editor 消费者 |
| 共享 cpu 标签测试 | 15/15 通过 |
| 共享 IPC、编辑器工作流及边界定向测试 | 首次 34/36；runtime-scene-loading 和 asset-workflow 失败。停止编译后两项独立复测 2/2 通过，未修改断言或超时；根因未确定，不能写成首次全通过 |
| 静态 cpu 标签测试 | 15/15 通过 |
| 静态 IPC、编辑器工作流及版本头定向测试 | 32/32 通过 |
| 共享版本头测试 | 修正后 1/1 通过 |
| 共享与静态指定前缀安装 | 均通过；启用 CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION，许可证落入各自前缀 |
| 共享安装包导入及最小链接 | find_package(gneiss) 与 gneiss::engine 链接通过；空 main 仅验证导入目标，不能当作 Engine API 或已安装 Runtime 验收 |
| 静态安装包独立消费者 | 配置未通过：当前安装前缀未提供 granitConfig.cmake；包声明要求外部 Granit 0.71.0 Window/RenderPipeline。静态仓内完整构建通过不能替代此项 |

安装包消费者需要的 Granit 开发包尚未就绪，本轮不修改第三方仓库或绕过 find_dependency。
后续提供独立 Granit 安装前缀，再验证静态导入、链接及真实使用；本轮未运行已安装 Runtime。

## 保留限制

本轮不启动神庙大场景、不执行专门 GPU 压力/设备丢失/回收测量，不修改历史像素断言。
PLAN-115 记录的 asset-pixel 失败、PLAN-119 提及的两项 GPU 基线失败需要逐项复现与定位，
不凭本轮 CPU 结果关闭。MSVC、Release、Linux GCC/Clang、Emscripten 和远端未验证。
本轮没有版本发布授权，不推送、创建 PR、合并或发布。
