<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-332：Application SDK 命名迁移

2026-10-16，按 ADR-062 实施，Application 相关包装迁至 `gneiss::sdk`，头文件路径保持不变。

| 原名称 | 新名称（均在 gneiss::sdk） |
| --- | --- |
| gneiss::application / application_ref | Application / ApplicationRef |
| gneiss::released_application | ReleasedApplication |
| gneiss::application_desc / application_callbacks | ApplicationDesc / ApplicationCallbacks |
| gneiss::application_platform / application_window_flags | ApplicationPlatform / ApplicationWindowFlags |
| gneiss::frame_time | FrameTime |
| gneiss::diagnostic / diagnostic_severity / diagnostic_category | Diagnostic / DiagnosticSeverity / DiagnosticCategory |

这是 C++ 源码兼容性调整，没有旧顶层名称别名；C ABI 符号和布局不变。方法及枚举值保持原命名。
相关示例、仓内调用方、安装消费者、类型与能力清单已同步；其他模块的包装暂保留原命名空间。
Editor/Runtime 本批仍使用 SDK，原生宿主接入单独推进，不以本次改名视为完成。

本地 Windows Clang Debug 全量构建通过，覆盖公共头独立编译。28 项相关回归通过，包含
C/C++ Application、原生对象、两版 C ABI 消费者、安装稳定 Runtime 消费者、SDK 能力清单及
负向检查、资源拥有/移动、异步场景和编辑器资产/Prefab 操作。未验证 Linux 和静态配置。

此前方案 A 的桥接实现已提交：桥接持弱引用；借用 destroy 拒绝；撤销不关闭原生对象；
对象关闭后的访问失效，槽位代次防止旧编号恢复。原生测试也覆盖错误线程和桥接移动。
完整 Application 迁移仍以 DEV-051 的宿主、原生配置/回调及依赖检查为完成条件。
