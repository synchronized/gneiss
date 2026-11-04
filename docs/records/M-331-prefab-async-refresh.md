<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：同源 Prefab 异步刷新与历史恢复

日期：2026-10-16。范围为 Editor 的 Refresh Prefab 入口，不代表 0.51 全部完成。

## 实现

- 手动刷新在后台准备来源、重新验证渲染依赖，所属线程分步捕获历史、构造隐藏实例和编辑器镜像。
- 同 URI 实例作为一条历史命令提交。World 先验证全部旧组和新组，再无分配切换可见性；任一候选失败
  保留全部旧实例、选择和历史。旧组含外部子节点时拒绝替换。
- 按用户确认的方案 A 重建节点及实体 ID，保留实例 UUID、作者覆盖与根变换。
  选择按实例 UUID 与来源 UUID 恢复；选中来源已删除时退回该实例根。
- 撤销、重做持有不可变来源和实际资产租约，不读取源文件；提交前可取消，提交后完成分步清理。
- 沿用现有模态进度与关闭排空路径。公共同步刷新 API 保持原语义；本轮替换 Editor 按钮的生产调用。

## 验证

Windows Clang Debug 构建相关目标后，8 项测试通过：Editor Prefab 编辑、作者操作、命令历史、
Editor smoke、Prefab runtime instance、Scene Prefab instance、C++ Prefab refresh、World 服务边界。

新增断言覆盖线程池与协作模式下两个同源实例的刷新、覆盖值保持、来源选择恢复、取消、源文件损坏后
撤销重做、第二实例的覆盖失效导致整批失败、来源删除后的选择回退，以及准备期间关闭场景。
World 测试覆盖后一个候选无效、重复组、跨 World、错误线程、外部隐藏孩子、退休组不能再次发布与重复清理。

`editor_prefab_edit.cpp` 与 `prefab_placement.cpp` 的 clang-tidy 无告警；World 测试新增部分无告警。
场景服务及 World 旧代码仍有既存静态告警，本轮没有关闭检查或扩大为无关清理。
`git diff --check` 通过。

## 限制

- 尚未进行大规模 Prefab 的交互耗时测量、Linux、安装消费者或完整版本矩阵验证。
- 每次推进限制节点数量，不承诺固定毫秒时限；拓扑准备、快照复制和整批提交校验仍可能随规模增长。
- 共享资产所有权迁出场景域仍待实施；本记录不宣称该项完成。

关联：[开发计划](../plans/DEV-051-architecture-boundaries.md)、
[异步放置](M-331-prefab-async-placement.md)、[异步删除](M-331-prefab-async-removal.md)。
