<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Runtime 检查来源关联外置

[PLAN-105](../plans/PLAN-105-game-object-runtime-model.md) 的 IPC 调用方迁移：
检查器通过 set_source_bindings 接收实例化地址回执，按实体完整句柄保存非拥有身份关联。
逐帧 capture_scene 不再读取 scene_node_info.prefab；未登记的动态运行对象按普通对象处理。
会话 reset 清除来源关联，重复地址或实体的回执拒绝并保留旧关联。

首次场景激活直接使用 scene_load_completion.objects。
旧来源重载尚未返回地址回执，因此宿主在其成功完成时收集一次关联；这处过渡代码仍读取
旧运行来源，必须随来源重载迁移删除，不代表运行 Prefab 状态已经清空。

回归使用通用重建创建没有 prefab_binding 的运行场景，再凭回执验证 IPC 的实例/来源复合身份。
默认 Windows Clang Debug 构建通过；IPC 场景协议、Runtime 场景检查、属性编辑、
编辑器运行镜像及 Runtime 进程 5 项回归通过。未运行 GPU 大场景或远端 CI。
