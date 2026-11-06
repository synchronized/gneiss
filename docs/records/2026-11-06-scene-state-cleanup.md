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

## 后续：视觉依赖失败隔离

- 结构重载改为预留稳定资源引用、同步更新结构、独立推进视觉队列；不等待所有视觉资源成功。
- 组件与 Prefab 绑定只要求资源身份有效；缺失文件不等于非法 URI。
- 材质纹理依赖逐项处理，失败继续其他槽位；首次加载使用默认贴图，更新失败保留旧材质内容。
- Application 视觉诊断保留依赖 URI 和消息；格式与层级失败仍阻断结构操作。
- 新增缺失网格、材质、纹理和更新失败回归，验证结构回执与视觉完成分离、稳定身份及旧内容。

Windows Clang Debug 完整构建通过；28 项相关回归中一项旧断言要求缺失资源阻断实例化，
改用非法 URI 验证失败回收后单独复测通过，其余 27 项通过；另一个 render_asset_loader 回归通过，
共 29 项。格式检查与 git diff --check 通过。
日志：build/scene-tolerance-build.log、build/scene-tolerance-tests.log、
build/scene-tolerance-retest.log、build/scene-tolerance-assets-tests.log（不入库）。
未运行 GPU 大场景、跨平台或远端 CI；缺失 Prefab、未知组件的数据保留未纳入此次实现。
