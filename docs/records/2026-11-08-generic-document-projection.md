<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 文档对象通用增量投影验收

## 变更

- 普通对象与 Prefab 展开成员共用按文档地址计算的增量投影，删除来源对象专用增删替换路径。
- 来源刷新不再强制整场景重建；同地址对象保留实体，新增/删除只影响对应成员。
- 删除无人调用的 prefab_replacement 与 preserve_nodes 分支。
- prefab_component_state 移到编辑器；测试的作者内容捕获改从编辑文档获取。

## 验证

Windows Clang Debug 默认构建通过。相关 84 项 CPU 回归中 83 项直接通过；
剩余编辑会话测试仍断言刷新后根 ID 改变，调整为身份保持后，重跑包含它的 6 项回归全部通过。
Prefab 用例同时检查来源改名、重挂接、删除、撤销重做、选择身份及冻结资源保活。
未运行 GPU 大场景和远端 CI。

## 剩余边界

初次实例化和旧运行捕获/来源重载仍保留 prefab_binding；本次没有把这些遗留实现视为已删除。
持久文档父地址与运行接口清理继续由 [PLAN-105](../plans/PLAN-105-game-object-runtime-model.md) 跟踪。
