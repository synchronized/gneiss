<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-294：Application 注册表与宿主入口拆分

日期：2026-10-05。内部 C++ 分层的阶段记录，不代表 0.45 全部完成。

## 实施结果

原 `application_api.cpp` 同时包含公共 C 入口、19 个私有宿主入口、Application 注册表与创建/销毁
实现。现按责任拆分：

| 位置 | 责任 |
| --- | --- |
| `src/application/application_registry.hpp/.cpp` | 唯一注册表、句柄解析、初始化成功后发布句柄、关闭与回收 |
| `src/application/application_host.cpp` | 现有宿主私有入口，委托同一注册表及对应服务 |
| `src/api/c/application_api.cpp` | Application 描述兼容、公共生命周期与日志入口 |
| `src/api/c/render_api.cpp` | Mesh/Material/Texture 与即时 UI、Debug Draw 的 C 适配 |
| `src/api/c/scene_api.cpp` | Scene Instance 与 Prefab 编辑的 C 适配 |
| `src/api/c/input_api.cpp` | 输入状态、事件与 Action 的 C 适配 |

Application 内部头迁为 `.hpp`，更新 Editor、Runtime 和测试的精确包含路径。
边界检查扩展到 Application，反例覆盖误引公共 RAII、定义 C 导出和内部绕回 Application C API；
反例夹具每次恢复基线，允许失败后重复执行。

## 私有宿主接口分类

这些接口均保留为同版本内部协作，不安装，不承诺稳定 C++ ABI；本次没有新增公共 ABI 功能。

- 帧捕获、渲染统计：Editor 及 GPU/性能验收使用。
- 外部 Executor 接入：由 Runtime/Editor 宿主组织任务执行器寿命。
- 场景请求、进度、取消、轮询与激活：Runtime/Editor 的场景候选及退休流程。
- 资源与纹理请求、进度、取消、轮询：宿主资产事务与验收使用。
- 资产、Scene 与 Prefab 重载：Runtime 资产重载路径。
- 日志提交：Application 与 Game Context 的共享内部入口。

临时解析得到的共享指针沿用已有调用保护，不构成第二份 Application 状态，也不承诺已关闭服务
仍可操作。既有关闭顺序、线程限制、回调约定与错误结果未作修改。

## 验证

共享库构建与 21 项相关检查通过（7.86 秒），包括 C/C++ Application、日志、Game Context、
场景与纹理异步加载、资源包装、旧 ABI 及安装消费者。头路径迁移后共享/静态全量构建均通过；最终专项共享 23/23、静态 23/23（11.52 秒）通过。

静态检查已执行；报告中包含既有公共宏的有符号位运算、固定宽度句柄的相邻参数及复杂度告警。
另外逐一对照拆分前后 50 个 C 入口，除创建/销毁委托注册表外，其余函数体保持一致。
没有关闭检查或放宽编译警告级别。本阶段未改变回调异常处理；完整回调契约仍属于 M-296 审查。

## 后续边界

应用内部配置仍复用已补齐并校验的公开描述；Render/Scene 部分内部方法也仍接收公开描述。
这些语义对象的细化、Reflection 内部注册接口、其余模块目录与完整 SDK 审查尚未完成。
本次没有运行远端发布矩阵，不以文件拆分替代版本验收。
