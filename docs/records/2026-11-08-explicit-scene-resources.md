<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景应用显式消费资源与投影重建

承接 [PLAN-105](../plans/PLAN-105-game-object-runtime-model.md)。

组件增改接口允许借用已准备的 scene_mesh_binding；传入后验证资源归属，并使用不带资产管理器
的组件上下文执行安装/更新。缺少引用返回 not_ready，不按 URI 再次创建加载请求。
apply_scene 的组件应用统一走该路径，资源引用不以当前缓存替换。

普通场景首次实例化使用空基线 apply_scene。编辑器 replace_prepared 重建也先组合完整有效对象，
随后通过同一创建函数安装；包含 Prefab 的重建不创建 prefab_binding，来源地址仅通过回执返回。
创建失败清理新场景；重建成功才交换场景槽位，保留外部场景句柄。

新增回归覆盖缺失绑定时缓存及文件读取不变、包含 Prefab 的重建返回来源地址但运行节点没有
Prefab 来源绑定，以及卸载清理。Runtime 含 Prefab 首次实例化及旧来源重载仍未迁移，
不将本阶段描述为全部运行对象已脱离 Prefab。

前阶段 entity_id 集合排序编译失败已修复为整数值去重；原验收记录同步更正。
后续构建命令在失败时中止，不再启动使用旧产物的测试。

Windows Clang Debug 默认构建通过，最终 84 项定向 CPU 回归全部通过。
未运行 GPU 大场景、远端 CI 或其他平台矩阵。
