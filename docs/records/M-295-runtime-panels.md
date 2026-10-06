<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：Runtime 面板与宿主分离

日期：2026-10-06。

## 实施范围

Runtime 层级树、选择查询、属性显示及编辑控件由 Apps 主程序迁至 `src/editor/runtime_panels.hpp/.cpp`，
归入 `gneiss_editor_ui`，读取独立 Editor 模型。窗口仍唯一持有选择，按会话与 generation 检查身份。

每次绘制借用宿主操作表，属性查询、写入、作者回写和结果报告同步执行；写入的属性值在 Apps 边界
适配 IPC。面板不持有协议、子进程或新模型，不改变原控件、旋转换算、确认状态和编辑行为。
其余作者场景、资产面板及工程装配仍待整理，本次未完成整个 Editor 薄入口。

## 验证

- Windows Clang Debug 共享配置全量增量构建通过。
- Editor、工程/IPC 和边界 CTest 50/50 通过，46.90 秒。
- 新增无窗口面板测试，位于 `tests/editor/`：过期会话、旧 generation、借用节点查询、只读绘制不触发修改、
  镜像关闭后选择失效。测试实际生成 ImGui 绘制数据，不依赖进程或 IPC。
- 面板编译命令没有仓库 Apps include；测试链接没有 Host、Project 或 IPC 协议库。
- 测试构建定义归入 `tests/editor/` 后，面板、Editor 冒烟与正反例边界专项 4/4 通过，2.81 秒。
- clang-format 与 git diff --check 通过；clang-tidy 仍报告迁移控件的 ImGui 有符号位标志、函数复杂度和初始化风格，未关闭检查。
- 本轮未重跑静态配置和远端矩阵；没有注入鼠标操作来验收每个属性控件，既有工作流不能替代完整人工交互验收。
