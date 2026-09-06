<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-196：安装后游戏模板验收

## 结论

安装验收现会检查 `${CMAKE_INSTALL_DATADIR}/gneiss/templates/game`，把模板复制到独立临时目录，
仅通过安装前缀重新配置和构建 Game Module，再由安装后的 `gneiss_runtime --smoke` 启动该工程。

Windows Clang Debug Shared 首次验证通过：模板配置、模块构建、动态加载、初始化日志、三帧运行和
关闭日志均成功，输出包含模块来源 `gneiss.template.game` 和正常 `stage=shutdown`。

缺失模块、错误 ABI、初始化失败和工程路径错误继续由既有 Runtime 项目与模块故障矩阵覆盖；模板
闭环不复制同一错误状态机测试。
