<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 源码目录与模块所有权

本文描述当前目录与构建归属。设计依据见 [ADR-128](../decisions/ADR-128-foundation-modules.md)，
迁移验收见 [PLAN-124](../plans/PLAN-124-foundation-modules.md)。

## 顶层目录

| 目录 | 职责 |
| --- | --- |
| engine/include/gneiss/engine | 仓内 C++ 模块契约；不是已发布的对象 SDK |
| engine/src | 实现、私有结构和后端适配 |
| runtime | 游戏宿主与运行进程入口 |
| editor | 编辑器、项目选择界面、文档与 UI |
| project | 项目配置与工程操作 |
| tooling | 离线导入、构建与发布工具 |
| protocol | 宿主间消息协议；不属于平台传输实现 |
| tests | 按被测领域组织的单元和集成测试 |
| cmake | 构建规则、第三方解析、安装与构建检查 |
| 3rd / vendor | 第三方源码与下载缓存 |
| examples / templates | 可运行示例与项目模板 |

## Engine 分层

目录表示职责，不要求每个子目录产生一个库。基础模块提供完整实现，Engine 不补齐其主体。

| 层 | 内容 | 构建所有者 |
| --- | --- | --- |
| core/base | GUID、字符串身份、类型 ID、引用、RID、UTF-8、路径校验、哈希 | gneiss_base |
| core/math | 数学值、相机参数与变换算法 | gneiss_math |
| core/data | value、值编辑操作、属性地址数据 | gneiss_data |
| core/meta | 类型反射、注册表、能力与借用视图 | gneiss_meta |
| core/serialization | 格式无关遍历与 JSON 适配 | gneiss_serialization / gneiss_serialization_json |
| core/events | 类型化信号 | gneiss_events（头文件） |
| core/tasks | 调度器、任务与唤醒契约 | gneiss_tasks |
| core/logging | 日志服务、默认绑定与订阅 | gneiss_logging；输出适配 gneiss_logging_sinks |
| platform/io | 平台异步执行器 | gneiss_io |
| platform/window | 窗口、原始输入、窗口 ID 和原生句柄 | gneiss_window |
| platform/process、dynamic_library | 子进程与动态库能力 | gneiss_child_process / gneiss_dynamic_library |
| resource | CPU 资产、描述、格式与语义验证 | gneiss_resource |
| function/files、ipc | 挂载、文件请求、IPC 传输 | gneiss_files / gneiss_ipc |
| function/asset、scene、render、input、application | 资产装配、运行世界与服务协调 | gneiss_engine |
| function/diagnostics | 主循环阶段与业务诊断 | 运行领域使用的契约 |

核心依赖方向为 Base → Math → Data → Meta → Serialization → JSON → Resource，箭头表示
被下一模块依赖。Files 依赖 Base/IO，窗口依赖 Base 与私有 Granit 适配；平台不依赖运行世界。

错误/result/assert 等通用入口仍位于 engine/include/gneiss/engine 根下，其基础实现归 Base。
类型身份 traits 位于 core/traits；当前文件包含数学类型的身份适配，不属于纯 Base 契约。
Data 的属性路径保留 meta 命名空间，但仅定义地址数据；反射解析与 variant traits 留在 Meta。
资源编辑策略属于 Resource，实体 ID 与运行对象接口属于 Function/Scene。

## 所有权与部署

共享构建中 Base 的身份表、Meta 默认注册表、Logging 默认服务各由一个动态库持有。
每个模块使用自己的导出宏。复制实现进宿主会破坏这种唯一性，普通测试必须链接生产目标。
需要注入静态 libassert 处理器的死亡测试单独使用同模块夹具，并在 CMake 中说明原因。

IO、Files 与 IPC 在共享构建中使用同一 libuv 动态库；静态构建复用上游 uv_a。
依赖解析脚本只创建第三方目标，不注入 Gneiss 业务源文件，也不重复编译 libuv。

Resource 不依赖运行服务。需要 Granit 或载荷准备的 mesh/texture/png 解码实现位于
function/asset/loaders；普通资产描述和二进制格式仍属于 Resource。
Window 只采集平台事件，Input 负责动作解释；原始事件不包含 Granit 类型。
运行循环的计时观察由 Function 维护，平台窗口不记录业务主循环阶段。

World 拥有 ECS、层级与场景成员；game_object 和 scene_instance 为借用句柄。
Prefab 覆盖、文档历史属于编辑领域，不能从运行 World 自动还原创作意图。
具体运行语义见[场景参考](../reference/scene-instance.md)。

## 构建与验证

基础消费者直接链接所需模块，例如 gneiss::meta、gneiss::serialization_json；不为了 JSON
处理链接 Engine。Engine 部署目标保留场景与渲染运行实现，Function 自己维护源码列表。
构建命令以[构建指南](../guides/building.md)为准，测试策略见[测试指南](../guides/testing.md)。
安装包导出链接目标和运行文件，不把仓内头文件契约描述成稳定公共 C++ SDK。

## 公共头路径

当前原生契约使用 `<gneiss/engine/...>`，位于 engine/include；不提供旧路径转发头。
这里的仓内模块契约不等同于稳定对外 SDK，安装包仍以链接目标与运行产物为主。

## 资源目录

resource 持有 CPU 资产与描述，function/asset 持有加载协调和需要后端能力的载荷准备。
文件挂载与 IPC 在 Function，IO 执行器在 Platform，不把服务实现下沉到 Resource。

### CPU 文档归属

资源描述只表达可保存的数据；编辑文档和撤销历史归 editor/documents，运行实例归
function/scene。共享描述不会自动获得运行世界、渲染服务或编辑会话的所有权。
