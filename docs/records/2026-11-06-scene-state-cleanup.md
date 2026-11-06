<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景加载内部状态收尾

日期：2026-11-06。计划：[PLAN-101](../plans/PLAN-101-scene-service-boundary.md)。

## 变更

- load_task 直接持有 scene_instantiation_data，删除 cpu_result 及其 shared_ptr 分配。
- 准备阶段统一检查 progress.phase，删除重复 preparing 标记。
- 私有 visual_load_state 集中保存队列、游标、session/revision、pending 和回执；
  推进、领取、取消、停止与清理由该结构处理，不新增服务。
- 取消保留 pending，直到对应取消回执被消费；保持视觉完成与场景结构激活分离。
- 资源引用每次最多预留 64 项，同步实例化、显式激活和旧 World 退场策略不变。

## 验证与限制

Windows Clang Debug 完整构建通过；23 项场景加载、Scene/Prefab 结构重载、实例化、
视觉资源、异步关闭、编辑器和边界回归全部通过。沿用已有生命周期用例，未新增重复测试。
修改的 C++ 文件 clang-format 检查与 git diff --check 通过。

日志：build/scene-state-build.log、build/scene-state-tests.log（不入库）。
未运行大场景、跨平台、远端 CI 或发布；未执行远端操作。
