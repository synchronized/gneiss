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

## 原生日志回调边界

Application 配置现在直接持有已有的 `log_internal::event_sink`，投递器消费原生事件。
C 日志布局转换移至 `engine/api/application_log_sink.hpp`；ABI 配置适配分配失败转换为结果码。
Runtime 使用捕获上下文的原生订阅，现有日志/IPC 消费接口的 C 值结构转换暂留在 Runtime 边界。
未新增日志队列、线程或第二套运行时状态。

Windows Clang Debug 相关目标构建通过，15 项日志、Application、IPC 和启动回归通过。
原生测试覆盖投递内容、序号、回调异常隔离及关闭前排空。其他生命周期回调尚待迁移。

## 原生平台回调边界

初始化、事件轮询和关闭改用 C++ callable；初始化和轮询使用已有强类型 `result`，
关闭标记使用 `bool&`。C 函数指针、上下文及 `uint8_t` 转换只保留在 ABI 创建入口。
配置复制 callable，不新增宿主状态；捕获的借用对象须保持到关闭完成。

回调抛出的分配异常映射为 out_of_memory，其他异常映射为 internal。
失败初始化仍配对清理；关闭回调失败仍释放后端，且不会在析构时重复调用关闭回调。
原生测试覆盖初始化异常、轮询关闭、轮询异常和关闭异常；15 项相关回归通过。

剩余：update、diagnostic、close_requested、时钟与主循环的句柄参数仍是兼容路径，
Application 整体迁移尚未完成。本轮未验证 Linux 或静态安装矩阵。
