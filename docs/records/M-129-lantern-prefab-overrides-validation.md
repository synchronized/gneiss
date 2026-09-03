<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-129 Lantern Gallery 差异化实例与本地验收记录

## 结果

Lantern Gallery 的三个灯笼继续共享同一份 Prefab，并分别为灯体平移、框架缩放和玻璃平移保存一条
实例局部覆盖。Runtime 工作流通过复合作者身份找到三个来源投影，并验证各自覆盖值互不干扰。

端到端验证还发现 Editor Runtime 镜像仍按单一来源 UUID 判重，会拒绝多个实例中的同源节点；现已
改为与 Runtime 一致的“实例 UUID + 来源节点 UUID”复合身份，并增加回归测试。

## 本地验证

- Windows Clang Debug 全量构建通过，110/110 测试通过。
- Windows Clang Static Debug 全量构建通过，108 项测试最终均通过。
- 静态矩阵首次运行时 `gneiss.runtime.stop-protocol` 因退出等待超时失败，单独重跑通过；共享矩阵中
  同一测试通过，记录为现有时序测试的偶发风险。
- Lantern Gallery Runtime 工作流验证三个差异化实例、Prefab 复合身份、运行属性写入、暂停/恢复及
  连续会话隔离。
- 工程版本已更新为 0.19.0，变更记录已补充本版本能力。

## 待验证

尚未推送分支，也未触发 Linux Shared/Static、Granit Runtime 和 Sanitizer Actions；这些远端操作需
获得用户明确授权后执行。远端矩阵通过前，0.19.0 保持“本地完成，待跨平台验收”。
