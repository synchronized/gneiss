<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 2026-11-08：文档历史与投影迁移首批

实施范围见 [PLAN-106](../plans/PLAN-106-document-history-projection.md)，决策见 ADR-111。

- 新增独立 project_scene_document 入口，接管恢复版本的运行更新。
- 已同步且只有有效 Transform 差异时原地更新；先检查全部目标和变换基线，再应用差异。
  组件、资源、名称或结构变化暂时完整重建。失败通过既有 projection_status 报告。
- 新增连续合并编辑及撤销重做后目标、无关对象 node/entity 身份不变的回归断言。
- 默认 Windows Clang Debug 构建通过，39 项相关 CPU 回归全部通过，git diff --check 通过。
- 验证期间误启动重叠构建导致静态库文件锁失败；等待原构建退出后串行重跑默认构建通过。
  未修改检查等级。未运行 GPU 大场景、远端 CI 或跨平台矩阵，未推送。
- 局部历史、独立编辑会话、完整结构投影尚未落地；版本历史仍是当前实现，不宣称计划完成。
