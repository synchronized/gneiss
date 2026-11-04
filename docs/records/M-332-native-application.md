<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-332：Application 原生对象首批迁移

## 状态

2026-10-16，本地完成原生真实对象命名、创建职责及直接调用回归。
这是 [DEV-051](../plans/DEV-051-architecture-boundaries.md) 的首批实现，不代表 Application
纵向迁移或 M-332 已完成。决策见 [ADR-062](../decisions/ADR-062-internal-cpp-and-sdk-boundaries.md)。

## 调用审计与调整

| 入口或类型 | 本轮前 | 本轮后及剩余职责 |
| --- | --- | --- |
| 内部实际对象 | application_internal::application_state | engine::Application，迁至 function/application/application.hpp/.cpp；原有业务状态直接迁移，无第二层对象 |
| 原生创建 | 注册表创建对象并 initialize | Application::create 接受已校验内部配置，成功返回共享持有，失败保留输出；构造和 initialize 私有化 |
| application_registry | 构造、初始化及注册 | 委托真实对象创建，只在成功后登记对外 RID；继续承担解析、线程校验和句柄销毁 |
| engine/api/application_api.cpp | C 描述版本补齐、配置校验、转换、注册调用 | 保留该适配职责；创建复用同一原生工厂 |
| application_host.cpp / game_context.cpp | 解析 Application 句柄后调用 state | 已指向新真实对象；仍有句柄桥接，宿主原生接口迁移未完成 |
| include/gneiss/engine/application.hpp 及 detail/application.inl | gneiss::application RAII 包装 | 本轮未改；后续迁入 gneiss::sdk 并同步完整消费者 |
| 原生配置/结果与回调 | application_configuration、gneiss_result、C 回调 | 尚未完整转换为原生语义；现有配置前置校验、回调协议保留，不宣称已经消除全部 C 类型 |

内部测试直接调用 Application::create 和 run，不创建 Application RID，也不调用 C 创建入口。
run 补充所属线程及已初始化状态检查；不依赖注册表代替对象自身的入口约束。
共享持有用于沿用现有注册表与调用期保活，最后一个引用必须在所属线程释放，不意味着支持
跨线程操作或延迟关闭后的资源访问。内部类头仍是私有装配头，尚不允许 Apps 任意依赖它。

## 验证

本地 Windows Clang Debug 构建通过，以下 5 项 CTest 通过：

- native-application：原生创建、无句柄主循环、错误线程拒绝、失败创建保留旧对象、失败初始化配对清理、最后引用析构清理。
- c_application、cpp_application：对外原有生命周期与 SDK 行为回归。
- scene-load-application、editor.prefab-edit：场景事务及 Prefab 编辑回归。

clang-tidy 检查原生实现、注册表和新增测试，仍报告原有主循环的复杂度、异常边界等告警；
未关闭规则。未执行 Linux、静态/安装消费者、完整 Sponza 或远端矩阵。

## 后续工作

先收敛原生配置/结果与回调适配，形成可供同源码宿主使用且不暴露私有布局的模块接口；
再迁移 Application SDK 名称、Editor/Runtime 调用方与安装消费者，补依赖反例。
当前布局包含平台编译开关；原生测试复制相应私有布局开关以保持定义一致，不能把这项测试接线
推广为公共安装依赖。World、Scene 和资产模块等待 Application 纵向闭环后继续。

## 后续：私有装配布局隔离

原生对象的唯一业务存储已移入 application.cpp；头文件通过不完整类型拥有该存储，
不创建额外运行时对象或资源表。23 个依赖字段的内联实现移入实现文件，原生测试已移除
对 Engine 私有平台宏的复制。此调整独立于类名大小写选择。
Windows Clang Debug 原生/C/C++ Application、场景加载及 Prefab 编辑 5 项回归通过。
宿主创建接入、完整原生配置/回调迁移仍未完成，不能以布局隐藏替代这些工作。
