<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-197：外部 SDK 消费与兼容文档收敛

## 结论

外部项目使用 `gneiss::engine` 作为完整运行库；`gneiss_runtime` 是加载工程和 Game Module 的游戏
运行宿主，`gneiss_editor` 是作者工具。Runtime 与 Editor 不作为普通库 target 导出，内部 IPC、
渲染执行器和 Granit 后端对象也不进入安装头。

安装 package 新增 `GNEISS_GAME_TEMPLATE_DIR`，稳定定位随 SDK 安装的模板。模板仍只链接
`gneiss::engine`，并在配置时检查 SDK 安装内容完整性。

兼容参考已更新到 0.30.0 目标状态，并记录 102 个公共 C 导出中 46 个 Stable 候选与 56 个
Experimental。Game Module 和日志尚未提升为 Stable，避免在动态模块、脚本及日志后端尚未充分验证
前扩大 1.x 承诺。
