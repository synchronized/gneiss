<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-124：基础模块、平台边界与构建归属

状态：实现中（2026-11-19）。决策见 [ADR-128](../decisions/ADR-128-foundation-modules.md)。

## 目标与范围

保留 core/platform/resource/function 分层导航；领域模块具有完整实现目标，基础消费者不链接
Engine 或渲染资源。迁移头文件、实现、实际消费者、测试、安装装配与文档，不保留旧路径转发层。
不改变资产格式、运行服务生命周期或异常策略，不新增公共 SDK，不进行远端发布。

## 已确认设计

- Core：base、math、events、tasks、logging、data、meta、serialization、通用 diagnostics。
- Platform：io、window（包含原始输入契约）、process、dynamic_library。
- Function：files（挂载、请求、预算）、ipc（传输与封装）、asset、scene、render、application。
- Resource 保持 CPU 资产与描述；依赖服务的加载器由 Asset 目标拥有。
- Base 不依赖数学、反射或平台；Data 依赖 Base/Math；Meta 依赖 Data；Serialization 依赖 Meta。
- 具体格式独立适配；JSON 依赖通用 Serialization，不反向依赖。
- CMake 依赖脚本只建立第三方目标；不注入 Engine 业务源码。

## 实施顺序与退出条件

- [x] P1：基础目标与符号归属。完整 Math；Base 纳入基础值与身份实现；模块独立导出控制。
- [ ] P2：Data/Meta。分离 property_path 的数据定义与运行时解析，移出 Data 的反射 traits；
  独立 Meta/Serialization/JSON 目标，迁移基础测试链接。
- [ ] P3：Logging/Tasks。日志完整实现独立，默认服务仅有一个所有者；后端可选。
- [ ] P4：Platform/Files/IPC。统一窗口输入契约，迁移文件和 IPC 服务；消除 IO 重复装配。
- [ ] P5：领域归属。entity_id、业务诊断、编辑策略移回领域，Resource/功能目标各自管理源码。
- [ ] P6：最终装配。静态/共享、安装包、真实消费者、跨模块身份与全量相关回归；同步当前文档。

每阶段源码只属于一个生产目标。不能仅完成文件搬移就勾选独立模块化。

## 链接与生命周期

有共享状态的模块在共享构建中必须由唯一动态库拥有，使用模块专属导出宏；不得宿主与 Engine
各静态嵌入一份字符串池、类型身份表、默认注册表或日志默认服务。静态构建使用同一依赖图，
安装包保留必要的链接闭包；不通过重新编译第三方源文件伪装闭包。
模块拆分不新增运行时初始化协议。服务仍由 Application 或工具显式创建、使用和关闭。

最小消费者：反射工具仅链接 Meta/JSON，注册类型、编码解码、处理非法输入并释放注册表；
文件工具链接 Files/Native，创建 IO、挂载、初始化、读取、分发完成、关闭 Files 后关闭 IO。

## 验证

阅读 testing/building 指南，优先复用行为测试；模块测试直接链接被测模块，不复制生产源码。
验证 Math/Meta/JSON 独立消费者、文件初始化与关闭、日志订阅、线程任务和 IPC。
共享/静态构建及安装链接验证是 P6 必需项；跨 DLL 比较字符串身份、type_id 和默认注册表。
独立头文件检查与边界检查更新到新路径，测试目标保持 FOLDER "Tests"。

## 风险与处理

Data 当前混有 Meta 适配，必须拆分而不是倒置目标依赖；value 对象引用的生命周期需独立核查。
现有 GNEISS_API 仅描述 Engine，不能直接用于多个共享库。先处理模块导出再移动有状态实现。
所有剩余事项在本计划维护，不将阶段构建通过写成整体完成。

## 阶段验收

2026-11-19：Math 的 transform/camera 实现移入独立目标，专属导出宏，安装目标包含 Math。
Clang Debug 共享与静态 Math 构建测试通过；共享 Engine 重新链接后的 render_camera 测试通过。
MSVC Release Math 构建测试通过。Math 消费测试不链接 Engine，并实际调用两份实现。
随后完成 Base 独立实现与专属导出：错误/Fatal、GUID、字符串池、运行类型身份与 SHA-256。
Engine 不重复编译这些实现，旧 core 目标引用全部迁为 base；Web 独立任务宿主也复用生产 Base。
新增共享库夹具验证跨 DLL 字符串身份与类型 ID，夹具与宿主直接链接 Base，不链接 Engine。
删除日志/Value 测试重复编译的字符串池/GUID 源码，以及三个数学测试重复编译的 transform 源码。

Clang Debug 共享全量构建通过，214 项 CPU 测试通过；静态 Base/Math、Fatal 与 Engine
render_camera 定向构建及测试通过。MSVC Release Base/Math 和跨 DLL 身份测试通过。
共享安装树 Runtime --smoke 完成场景加载、首帧与正常关闭，Base/Math DLL 随包安装。
core-boundary 和 git diff --check 通过。静态安装消费者、Web 和跨平台验收仍未完成。
P2—P6 尚未完成；Base 头文件与实现的物理路径归位随 P5 完成，不将目标独立等同目录迁移完毕。
