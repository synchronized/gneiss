<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# UPSTREAM-051：CAORS 接入约束与上游改进建议

## 状态与用途

2026-10-19，待 CAORS 评审的需求交接文档。用户负责交给 CAORS 处理；尚未接入 Gneiss，
本文件不表示已经决定替换资源或反射实现，也不代表已经向上游提交 Issue/PR。

源码检查基线为 CAORS 提交 `b46acead0b9aa568de10c936e7d40ac06b9ed2b6`。
下文源码路径均相对于 CAORS 仓库根目录，方便在上游独立阅读。
已有实现、源码推导的风险和建议行为分别列出；风险不等于已复现的缺陷。

目标是让 CAORS 安全地作为 C++20 宿主的内部依赖，明确跨 DLL、资源寿命、构建与调度契约。
不要求所有 C++ 接口改为 C ABI，不要求移除 Asio 或改用 libuv，不开发 Gneiss 插件系统。
Gneiss 的 GPU 上传、RID、场景事务、Prefab 和撤销重做不属于上游范围。

## 优先级与建议拆分

| 编号 | 事项 | 优先级 | 当前状态 |
| --- | --- | --- | --- |
| C51-01 | 跨 DLL 与 runtime 身份契约 | P0：接入前明确 | 待评审 |
| C51-02 | 资源、allocator 与模块卸载寿命 | P0：跨模块资源使用前明确 | 待评审 |
| C51-03 | 可嵌入的 CMake 配置 | P1：联合构建前处理 | 待评审 |
| C51-04 | PIC、符号与安装包验证 | P1：分发前验证 | 待评审 |
| C51-05 | 共享加载的独立取消语义 | P1：多消费者加载前处理 | 待评审 |
| C51-06 | 外部 executor 适配 | P2：按宿主调度接入需求处理 | 待评审 |

建议按“构建与 ABI 契约”“资源寿命”“共享请求与调度”拆为可独立评审的改动。
上游可提出更小的实现方案；先明确行为和验收，不预设具体类名或函数签名。

## C51-01：跨 DLL 与 runtime 身份契约

### 已有实现与依据

- `include/caors/meta/type_id.h`：类型 ID 由共享 runtime 分配，是进程内索引，不是持久化 ID。
- `include/caors/core/string_name.h`、`src/caors/core/string_name.cpp`：字符串驻留表位于实现中，
  `string_name` 按条目地址比较及哈希。
- `include/caors/meta/registry.h` 等普通接口包含 C++ 对象和标准库类型。
- `src/caors/plugins/plugin_binary.cpp`：插件路径检查编译器、CRT、迭代器调试级别、指针大小和架构。
- `tests/meta/plugin_identity/`：验证共用 CAORS 动态库时的跨模块身份。

### 限制与建议

EXE 和多个 DLL 各自静态链接独立 CAORS 后，其 runtime 状态不能假定统一。
类型 ID 可能不一致或发生数字碰撞，来自不同驻留表的同名字符串也不能依赖地址相等。
普通链接 CAORS 动态库不会自动执行插件加载协议中的构建环境检查。

请明确支持以下部署方式及前提：

1. 多模块共用同一版本 CAORS 动态库，在兼容的 C++ ABI 配置下交换对象。
2. 单个宿主模块私有静态链接 CAORS，CAORS 对象和 runtime 身份不跨该模块边界传播。
3. 独立 runtime 之间仅通过明确的值协议交换数据，不直接互传内部身份或对象。

文档列出工具链、标准库、CRT、Debug/Release ABI 配置和版本匹配要求。
同为 C++20 不代表 ABI 兼容；稳定 GUID 与 runtime type_id 的用途须明确区分。
不要求实现任意工具链或多版本 CAORS 对象混用。

### 验收

- EXE 与两个 DLL 共用 runtime：验证类型 ID、字符串相等性与反射查询。
- 增加跨模块对象创建与释放覆盖，标明测试工具链和链接方式。
- 不兼容插件配置有明确诊断；普通链接路径的责任边界写入文档。
- 对不支持的多 runtime 对象互传明确说明，不以共享 DLL 测试推导其安全性。

## C51-02：资源与模块卸载寿命

### 已有实现与依据

`include/caors/resources/handle.h` 的控制块保存销毁回调及 allocator 指针；
通用控制块未自动携带 DLL lease。反射 registry layer 已提供关联寿命的机制，
但这不能推出所有资源、回调与 allocator 都自动受模块卸载保护。

### 风险与建议

模块创建资源，宿主持有强引用，模块先卸载，再释放资源时可能调用已卸载的析构代码。
即使强引用全部释放，弱引用控制块仍可能需要访问原 allocator。

建议提供可选模块寿命 token，或明确由宿主 pin 模块并遵守卸载协议。
优先复用 `plugin_library_lease` 及已有寿命机制，避免重复设计。
契约必须覆盖销毁函数、allocator 对象及实现代码、异步任务和完成回调。
最后一个强引用释放并不必然意味着所有模块依赖均已结束。

### 验收

- 仍有强引用时请求卸载，不调用失效代码。
- 强引用释放但仍有弱引用时，最终控制块释放安全。
- 资源创建失败、取消和加载失败路径不泄漏模块引用。
- 后台任务及完成回调未结束时，卸载按契约延迟或被拒绝。
- 模块寿命引用不形成导致永久无法卸载的循环。

## C51-03：可嵌入的 CMake 配置

### 已有实现与依据

`CMakeLists.txt`、`src/CMakeLists.txt`、`3rd/CMakeLists.txt`、`cmake/utility.cmake`：

- 库类型与 `CAORS_STATIC_DEFINE` 依赖全局 `BUILD_SHARED_LIBS`。
- Catch2、ctrack 的目标创建没有完全按用途开关隔离。
- 已有 `Catch2::Catch2` 时跳过自身定义，未保证 `Catch2WithMain` 同时存在或核对兼容版本。
- `target_compile_warnings`、`USE_LOG` 等通用名称可能干扰父项目。

### 建议与验收

1. 提供 CAORS 独立链接类型选项，默认可跟随父项目；导出宏依据目标实际类型生成。
2. 测试框架和 benchmark 依赖按需配置；关闭相关功能后不额外构建这些目标。
3. 支持父项目已有完整 Catch2 targets 或仅有基础 target；缺失依赖给出明确诊断。
4. 复用外部依赖时检查所需兼容版本，避免静默采用未知版本。
5. 函数和选项使用 `caors_` / `CAORS_` 前缀；如保留旧名称，明确兼容及优先级。
6. 验证宿主共享、CAORS 静态，以及宿主静态、CAORS 共享两种组合。

## C51-04：PIC、符号可见性与安装包

### 已有实现与依据

当前 CAORS target 未显式设置静态 PIC、默认隐藏符号或 `VERSION` / `SOVERSION`。
这不等于所有构建必然失败，父项目或工具链可能提供对应配置，但接入契约尚需明确。
Asio 是私有实现依赖；CMake `PRIVATE` 不等于符号隔离，也不意味着静态链接依赖自动消失。

### 建议与验收

- 支持 Linux PIC 静态 CAORS 并入另一个共享库。
- 检查动态库导出列表，明确公共符号，避免无意导出内部实现与 Asio 符号。
- 定义版本兼容政策及动态库版本标记；版本文件或同名 DLL 不能替代 ABI 保证。
- 静态安装包正确表达所需系统链接依赖，不依赖原源码或构建目录。
- Windows、Linux 分别验证共享和静态安装后的独立 consumer。
- 补充宿主共享库私有使用静态 CAORS 的 consumer 验证。
- 记录实际 DLL/SO 依赖及导出符号检查结果；不同版本共存若不支持，应明确写出。

## C51-05：共享请求的独立取消

### 已有实现与依据

`include/caors/resources/async.h` 和 `docs/resources.md`：同类型同 GUID 的进行中请求可复用；
任意共享句柄调用 `cancel()` 会取消共享状态。这是当前契约，不应直接定性为实现缺陷。

### 需求与建议

多个消费者共享底层加载时，一个消费者退出不应默认中断其他消费者。
建议区分取消订阅者等待与取消整个底层任务，明确全部订阅者退出后的策略。
如保留全局取消能力，应以明确的接口表达，不能让两个含义混淆。
共享状态句柄的普通复制与独立订阅也应区别定义。

### 验收

- A、B 请求同一资源，只执行一次底层加载。
- A 取消，B 仍可成功；A 不再收到成功应用结果。
- 全部取消后按策略结束，不出现永久 pending、重复提交或泄漏。
- 覆盖取消与完成竞争、依赖预加载、请求失败及缓存提交前取消。
- 回调执行线程和恰好一次的终态交付契约明确。

## C51-06：外部 executor 适配

### 已有实现与依据

`include/caors/tasks/executor.h` 的构造入口主要由 CAORS 执行环境提供，
宿主已有线程池不便直接包装为 executor。CAORS 私有使用 Asio 与宿主使用 libuv 并不构成版本冲突，
但可能引入重复线程池、完成队列和关闭流程。

### 建议与验收

- 提供受控的外部投递适配接口，不泄漏 Asio 类型，不要求宿主使用 CAORS 自有线程池。
- 明确投递成功后的任务所有权、拒绝投递、关闭、任务丢弃及上下文寿命。
- 后台加载交给宿主线程池，完成提交交给宿主所属线程队列。
- 关闭或拒绝任务时请求进入明确终态；适配上下文销毁后不能访问悬空回调。
- 不改变资源 cache 的所属线程约束；同步 inline 执行应明确重入语义。

## 依赖检查范围与验收证据

源码检查发现 CAORS 私有使用 Asio 1.38.2，配置 Catch2 3.15.0、ctrack 1.1.0。
Gneiss 使用 libuv 1.52.1，实际检查的 Granit 依赖也附带 Catch2 3.15.0。
本轮未发现这些声明中存在已发生的同库版本冲突；不代表任意父项目或版本组合安全。

本轮执行了 CAORS 已有 `build/windows-msvc-shared-debug` 构建中的
`ctest --test-dir build/windows-msvc-shared-debug -R 'caors.meta.plugin_identity' --output-on-failure`，
结果 1/1 通过。没有重新构建该目录，不将已有二进制测试视为当前源码完整验收。
尚未执行 Gneiss 与 CAORS 联合构建、多版本混用、Linux 符号隔离及上述新增验收矩阵。

Windows 跨 CRT 分配释放风险的依据：
[Microsoft：跨 DLL 边界传递 CRT 对象可能的错误](https://learn.microsoft.com/zh-cn/cpp/c-runtime-library/potential-errors-passing-crt-objects-across-dll-boundaries)。

## 上游回复与 Gneiss 接入顺序

请上游对每项反馈：接受、已有等价能力或不支持；提供对应接口、寿命契约、提交及测试证据。
若已有能力满足需求，应补充使用说明和缺失测试，而非创建平行接口。

Gneiss 侧建议先验证单一 Engine 模块内部使用 CAORS，锁定提交并统一构建环境；
公共 SDK 不暴露 CAORS 类型。具体静态或共享布局须在接入 ADR 中确定，本文件不替代该决策。
上游约束明确后，先验证一个 CPU 资源及一个反射类型，再决定是否扩大迁移范围。
