<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 子树快照归属 Editor 文档

日期：2026-11-08。对应 [PLAN-105](../plans/PLAN-105-game-object-runtime-model.md)、
[ADR-110](../decisions/ADR-110-game-object-runtime-model.md)。

## 实施范围

删除 scene_instances 和 scene_instance_state 的 capture_subtree/restore_subtree。
产品代码已不调用这组接口；删除重复的运行来源遍历、快照资源收集、身份重映射、
临时场景成员集合和恢复提交实现。保留普通创建、组件安装与递归删除能力。

scene_subtree_snapshot、scene_object_resources 及其内容恢复函数迁入
editor/documents/scene_document_snapshot.hpp/.cpp，命名空间改为 gneiss::editor。
删除 Engine 的旧头文件、构建列表项及已无调用的 scene_subtree_max_nodes 常量。
不添加 Engine 到 Editor 的反向依赖。

## 验证范围

运行组件和场景实例测试改为消费明确描述/资源进行重建，验证组件析构、资源引用复用及
旧实体 generation 失效，不通过保留已删除接口维持旧测试。
子树历史仍由既有 Editor 测试覆盖：混合普通/Prefab 层级、多实例、源文件损坏后恢复、
缺失资源失败、复制身份重映射、独立修改、重复恢复拒绝。

Windows Clang Debug 默认构建通过；构建成功后运行 84 项相关回归，全部通过
（82.43 秒）。日志：`build/plan105-subtree-build.log`、
`build/plan105-subtree-tests.log`。修改行 clang-format 无进一步变更，
`git diff --check` 通过。Engine/Runtime 检索无上述快照类型和捕获/恢复接口残留。

## 剩余边界

运行 capture_description/serialize 和旧来源重载仍存在；本次不宣称全部运行作者状态已删除。
手动 instantiate_prepared 中的旧 Prefab 创建路径，以及持久父关系的对象地址迁移仍在计划内。
未运行 GPU 大场景或远端 CI，未执行远端操作。
