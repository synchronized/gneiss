<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Runtime 会话所有权验收

日期：2026-11-22。范围：[PLAN-128 P3](../plans/PLAN-128-host-business-ownership.md) 的所有权与构建边界阶段。

## 实际改动

runtime::session 拥有 World、world_render_binding 和 runtime_scene_loader，只借用 Application。
服务就绪后 initialize；synchronize 刷新 World 变更并同步渲染，append_views 汇总相机视图，宿主统一提交帧。
关闭先停止加载回调，宿主解除检查器及重载借用，再由会话释放加载器、绑定和 World。
渲染背压、加载失败和取消语义保持原路径。

新增 gneiss_runtime_host 编译会话、场景加载、资产重载与检查业务，原 gneiss_runtime_ipc
只保留通信处理。Runtime 链接两个目标；六个业务测试改为依赖 host，不重复编译生产源码。

## 验证

Windows Clang Debug 共享配置下 Runtime 与受影响测试构建通过。14 项定向 CTest 全部通过：

- runtime.ipc-session、ipc-command、scene-inspection、scene-loader、property-editor、asset-reloader。
- runtime.smoke、startup-diagnostic、stop-protocol、async-input-startup。
- scene_structural_reload、prefab_structural_reload。
- editor.runtime-process、runtime-scene-loading。

以上测试名均以 gneiss. 为前缀。覆盖独立运行、Editor 子进程连接与停止、加载与结构重载。
源文件 clang-format 检查与 git diff --check 通过。

## 剩余范围

P3 尚未完成：loading() 是借用加载器的过渡入口，协议类型、重载回调与检查状态仍需从
application.cpp 收敛到业务接口。host 仍依赖共享协议类型，不宣称业务已与协议完全解耦。
IPC 已解析 pause_game/resume_game，但当前 Runtime 入口没有消费这些动作；需要补齐实际业务
调度和暂停恢复验收，IPC 命令测试通过不代表游戏系统已经暂停。

本轮未执行静态库、跨平台、完整 GUI 连续操作或版本发布矩阵。P4 工作区及其他目标整理仍待实施。
