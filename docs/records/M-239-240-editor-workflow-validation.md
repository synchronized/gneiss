<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-239/M-240：资产闭环与编辑器自动化验收

## 资产工作流

2026-09-25，新增真实连续集成用例 `gneiss.editor.asset-workflow`：创建临时工程，导入原创 glTF，
将导入 Mesh/Material 加入场景并保存，启动 Runtime，将源材质颜色从 0.5/0.6/0.7 改为
0.9/0.2/0.1，不发送文件通知而请求补扫，确认派生材质改变且 Runtime 返回 applied。
随后导出 Debug 配置目录包、校验清单，并启动包内 Runtime，确认进入 running 后正常退出。
使用持续运行的测试模块；已有第 99 帧故障模块行为保持不变。本测试验证状态与资产内容，
不做 GPU 像素颜色读回。Shipping 打包由既有安装 Runtime 用例补充验证。

## 交互自动化范围

本地 Windows Clang Debug，作者文件监视、Runtime 进程、上述资产工作流、UI 布局、Scene Session、
相机、命令历史、Gizmo 矩阵共 8/8 通过。原生 Editor 进程通过 Win32 SetWindowPos 依次得到
984×601、1584×861、1264×681 客户区，进程保持运行且 WM_CLOSE 后返回 0；查询 DPI 为 96。
探针与结果保存在本地 `build/036-window-probe.py/.json`，未将临时脚本作为产品功能提交。

M-240 的本版本验收限定为上述自动化范围：不将层级选择/Session 测试当作视口拾取验收，
不宣称真实鼠标 Gizmo 拖动、全套拾取、窗口缩放画面读回或跨显示器 DPI 已人工验证。
这些限制随 Release 公开，作为后续桌面交互验收项，不扩展为本版本新增编辑器功能。

## 静态检查

新增代码按 clang-format 排版；运行作者监视与工作流测试的 clang-tidy，修正新测试的缺失大括号
和尾逗号。既有 noexcept 路径内分配异常、测试复杂度等告警未全面清理，未关闭编译警告或降低
测试等级。跨平台结果另见版本发布记录。
