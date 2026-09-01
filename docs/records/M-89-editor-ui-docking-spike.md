<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-89 Editor UI Docking Spike 记录

## 结论

Dear ImGui 官方 Docking 分支可以直接复用现有 Gneiss UI Draw List 与 Granit Canvas 渲染路径。
最小主 DockSpace 已在 Windows VS2022 Shared Debug 编译并通过 Editor 工作流测试，未发现需要 Granit
新增底层能力的缺口。M-89 完成，可以继续提取 `gneiss_editor_ui` 内部模块和默认布局。

## 候选依赖

| 项目 | 值 |
| --- | --- |
| 仓库 | `https://github.com/ocornut/imgui.git` |
| 分支 | 官方 `docking` |
| 锁定提交 | `fd13a1e8923a0a7077b404fc36fd063b25a0c0b5` |
| 上游版本标识 | `1.93.0 WIP` |
| 许可 | MIT |
| 原依赖 | 主分支 `v1.92.9b`，提交 `f1cc2ae15e53a861a874c3034aae6798fde194ab` |

选择精确提交而不是浮动 `docking` 分支。WIP 标识意味着后续升级必须显式修改哈希并重新执行本记录
的编译、Draw List、布局和工作流验证，不能假设分支头兼容。

## Spike 范围

- 在 ImGui Context 初始化时启用 `ImGuiConfigFlags_DockingEnable`。
- 每帧创建单窗口主 `DockSpaceOverViewport`。
- 保持 Multi-Viewport 关闭，不创建额外原生窗口、Renderer 或 Swapchain。
- 保持 `io.IniFilename = nullptr`，本阶段不提前启用未受控的 `imgui.ini` 持久化。
- 保持现有字体 Texture RID、ImGui Draw List 转换、Scissor、索引和同帧 Present 路径。

## 验证结果

| 验证 | 结果 |
| --- | --- |
| Dear ImGui 精确提交解析 | 通过，构建树检出锁定提交 `fd13a1e` |
| `gneiss_editor` Windows VS2022 Shared Debug 编译 | 通过 |
| `gneiss.editor.smoke` | 通过 |
| `gneiss.editor.project-manager-smoke` | 通过 |
| `gneiss.editor.lantern-gallery-project` | 通过 |
| `gneiss.editor.lantern-workflow` | 通过 |

Docking 生成的数据仍通过现有 `ImDrawData`、顶点、`uint32_t` 索引、裁剪矩形和 Texture RID 提交，
没有要求公开 Granit Renderer、平台窗口或后端句柄。因此当前不向 Granit 提出 PR。

## 后续边界

- M-90 提取 Context、主题、字体、主 DockSpace 和通用工具栏到 `gneiss_editor_ui`。
- M-91 使用 DockBuilder 构建确定性默认工作区；当前 Spike 不承诺默认停靠位置。
- M-92 在用户配置目录实现版本化布局保存；当前仍不写布局文件。
- Linux、静态链接、安装树和布局交互在 M-94 统一验收。
