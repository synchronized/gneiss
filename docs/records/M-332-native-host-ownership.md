<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-332：原生宿主拥有 Application

2026-10-16，按已确认方案 A 接入 Editor、Runtime 和 Project Manager。
三者直接创建 engine::application，并直接调用主循环、获取 World 和装配任务执行器。
配置使用内部窗口和环境字段，不再为创建拼装 C 描述或使用 SDK 拥有包装。
原生创建校验窗口和环境参数；失败保留原输出，未进入初始化回调。

尚未迁移的场景、资产、UI 及 Game Context 接口通过内部 application_bridge 借用句柄访问
同一个对象。桥接先于宿主对象析构；请求/模块清理仍在对象和任务执行器存活期间执行。
这不代表所有模块的 C 入口已移除：本轮回调签名和结果码仍使用原 ABI 表达，待继续收敛。
Editor 调试绘制仍使用 SDK 借用操作，但不参与 Application 的创建、拥有和关闭。

本地 Windows Clang Debug 构建成功，7 项相关回归通过：native-application、c_application、
cpp_application、runtime.smoke、runtime.startup-diagnostic、editor.smoke、
editor.project-manager-smoke。原生测试额外覆盖无效环境参数失败不替换输出。
未执行 Linux、静态安装矩阵或完整 Sponza，不将启动测试等同大型场景验收。
