<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# DEV-052：原生接口与模块职责整理实施计划

## 状态

2026-11-04：已完成 Meta 操作表拆分与容器 traits 适配，见[验收记录](../records/2026-11-04-meta-type-ops.md)；全仓审计及其他批次仍待开始。版本范围和非目标见
[VER-052](VER-052-0.52.0-architecture-cleanup.md)，本页仅维护实施顺序和验收方法。

## 当前批次

已完成容器视图按 sequence、associative、optional 分拆，并增加 reference_view，
见[本批验收记录](../records/2026-11-04-meta-views.md)。
迁移序列化和调用方，移除 instance 上的旧引用专用入口；借用、共享控制块及文件格式保持不变。
验收覆盖空引用、只读目标、共享赋值、重置、类型不匹配、深层 JSON 和独立头编译。

## 实施顺序

### A52-01：建立现状与删除清单

- 沿“文件读取 → CPU 解码 → 运行资源 → Scene/World → Editor”检查拥有、借用和关闭顺序。
- 阅读实际调用方，列出重复转发、状态、值输出参数、实现头暴露及已失效的文档说明。
- 将每项标记为待删除、待合并或保留并说明原因；不依据文件大小直接拆类。
- 核对 AGENTS、当前 Guide/Reference 与 0.51 已发布行为；旧 Plan 仅增加必要的历史状态说明。
- 交付一份有源码路径的审计记录和批次列表，作为后续改动依据。

验收：每个拟修改入口能指出调用方、所有者和替换位置；已完成的 0.51 工作不重复立项。

### A52-02：原生值返回与异步契约

- 优先审查 Application 创建、场景请求/完成、资源请求/完成，再检查 World/Scene/Meta 的剩余接口。
- 有自然成功值的接口优先使用 `result<T>`；查询不存在、尚未完成和操作失败分别明确，不增加含义重叠的布尔标记。
- 对外请求编号、取消目标及完成消费规则保持一致；先核对现有契约，再决定是否需要统一表示。
- 同批迁移调用方和测试，删除旧签名；不批量替换原地修改参数或失败诊断输出。

验收：相关模块编译通过；成功、失败和未完成状态可区分，取消不会消费其他调用方结果。

### A52-03：生命周期与业务协调归属

- 核对 Resource Service 的在途请求、弱缓存、资源引用及退休队列的关系，删除经证实重复的状态。
- 核对 Scene 实例、Prefab 绑定、捕获快照与编辑历史的资源保活；保持运行数据的单一权威来源。
- 沿 `document_editing`、场景加载协调和宿主关闭链路检查重复推进/取消/清理。
- Application 保留装配、循环和生命周期协调；已有领域所有者能承担的业务逻辑归还其所在模块。
- 每一处移动都同步删除旧转发或说明其必要职责，不新增平行协调器。

验收：小型场景切换、失败/取消、重复关闭、撤销重做、资源释放回归通过；不引入析构时阻塞推进任务。

### A52-04：文件布局与编译依赖

- 按前述职责整理 resource、function、runtime、editor、host 的文件归属。
- 减少只需前置声明却包含实现头的依赖；按现有 CMake target 明确包含和链接传播。
- 按已有命名规则整理，不另起第二套全仓命名标准；保持 `math`、`meta`、文件服务专用命名空间。
- 扩展已有边界检查及必要反例；自有共享库导出不向调用方泄漏后端类型。

验收：相关独立头检查、边界反例、共享/静态链接通过；无旧路径转发头和无用途占位目录。

### A52-05：集中验收与收尾

- 汇总每批删除项、保留理由和行为变化，更新当前接口及架构说明。
- 运行场景、资源、Meta、文件、Editor/Runtime 的相关回归及小型图形工作流。
- 检查 CPU/GPU 测试分类；有 GPU 环境验证窗口和像素用例，无 GPU 环境保留 CPU 覆盖。
- 达到候选状态后按已获授权范围执行完整平台验证；发布前确认 MSVC/GCC/Clang、共享/静态及相关 Sanitizer 结果。
- 记录未测范围及遗留风险；发布操作另按本版后续授权执行，不继承 0.51 授权。

## 验证节奏

开发期间每批先完成受影响模块编译和直接相关行为测试，不反复运行大场景或完整远端矩阵。
涉及模板、导出或跨平台头文件的修改应尽早检查 MSVC 与 GCC/Clang 差异，避免发布时集中暴露。
完整矩阵在候选形成后集中执行，纯验收文档更新不重复触发。
测试命令以[构建指南](../guides/building.md)为准，不降低警告或原验收门槛。

## 交付控制

所有批次使用同一版本分支和聚焦本地提交。每批记录实际实现、验证及删除入口，
不以“继续整理”为由无限扩大版本范围。审计若发现某项已充分收敛，记录证据后保留现状；
若出现新的架构取舍，先更新本计划或 ADR，再实施相应变化。

## GUID 空值与可选反射收敛

已完成，见[本批验收记录](../records/2026-11-04-guid-parent-and-meta-optional.md)。

按用户确认：完整对象 parent_uuid 使用 guid，nil 为根；部分修改使用现有属性路径或明确场景指令。
删除 Meta optional 类别、视图、操作表及 JSON 分支；不禁止业务内部 std::optional。
迁移源码、示例资产和生成脚本；后续按用户确认，GUID nil 的 JSON 表达统一为 null。验证 nil 往返、非零身份约束、
缺失父节点/循环拒绝、Prefab 根约束以及重新挂接/撤销操作。

GUID nil 的 JSON 表达调整已完成，见[验收记录](../records/2026-11-04-guid-json-null.md)。

## 序列化重复逻辑收敛

本批四项范围及状态：

1. 已实现：Prefab 属性覆盖值复用 Meta 标量 JSON 编解码。带类型标签的 value
   使用统一标量规则；GUID 标签为 `guid`，值为 UUID 字符串或 null，不保留旧 `type_id` 标签。
   覆盖目标的 `type_guid` 身份键已在后续收敛为规范 UUID 字符串。
2. 已完成：导入 SDK 链接 Engine，构造材质/场景/纹理描述并调用共享 codec，
   删除手写 JSON 及重复编译的编码/摘要实现；见 [ADR-102](../decisions/ADR-102-importer-engine-codecs.md)。
3. 已实现：Scene/Prefab 文档对象统一调用 `scene_codec.cpp` 的 `encode_object`，删除
   文档写入过程重复的组件与 Prefab 覆盖编码；文档级身份和层级校验保留。
4. 已完成评估，纹理 usage 与显式 codec 随导入器迁移，纹理反射迁移已实施，网格仍为评估方案。

### 导入器依赖决策

用户已确认直接链接 Engine，暂不抽取共享模块。导入只初始化 Meta，不启动 Application 或 GPU。
验收：生成资产可由 Engine 读取，稳定 UUID、材质参数、组件和纹理语义正确，URI 正确转义，
失败不产生部分正式输出。具体权衡以 ADR-102 为准。

### 纹理与网格评估

- `texture_description` 原先只有 URI 与色彩空间，本批已补齐 usage 保存及共享编码入口，
  纹理字段已注册反射，字符串枚举通过 enum_name 标记保留。normal 必须使用 linear 的业务校验继续保留。
- `mesh_decoder.cpp` 的 JSON 使用独立顶点/UV/法线数组，内存 mesh_data 的排列不同。
  建议明确文件描述与运行载荷转换，反射负责字段读取，转换负责数组长度、三角形数量、
  有限数和法线检查，不直接序列化运行数据的内存布局。
- 纹理压缩、mip、二进制网格及固定字节布局继续使用专用 codec；不为减少 switch
  强行迁移到通用反射。

本批验证见[导入器共享 codec 验收记录](../records/2026-11-04-importer-engine-codecs.md)。
前批覆盖值/对象编码的三项测试亦已通过；未执行大场景、远端 CI 或跨平台矩阵。

### 三项序列化收敛验收

构建工具 read_texture_settings 复用纹理 codec；纹理字段走反射和通用格式包装；Prefab 覆盖
目标 GUID 使用 guid::parse/string，与其他身份字段统一。当前样例、夹具和格式参考同步迁移，
旧 32 字符覆盖类型标识不保留兼容。详见[验收记录](../records/2026-11-04-texture-reflection-guid.md)。

### 显式多态与组件引用迁移（已完成）

决策见 [ADR-103](../decisions/ADR-103-inline-polymorphic-references.md)。

1. Meta 提供 object 身份、polymorphic/base 注册和引用按值 JSON；删除自动对象表。
2. 组件改为不可变基类 ref，Scene/Prefab、编辑器与 IPC 复用通用反射入口。
3. 覆盖引用语义与类型安全测试，编译受影响目标，更新当前接口参考与验收记录。

Windows Clang Debug 全目标构建及 26 项相关测试通过，见
[验收记录](../records/2026-11-04-inline-polymorphic-references.md)。本批完成不表示整个 0.52 版本完成。

### 组件数组 DOM 入口收敛

已将 Scene 与 IPC 的组件数组读写统一到 description_codec 的内部适配入口。
Meta 负责临时 vector<component_description> 的数组及多态内容，业务层校验非空和类型唯一后
生成 component_descriptions。IPC 不再经过 JSON 字节中转；普通组件不再拆解重建类型信封。
Prefab 组件覆盖仍含业务定位字段，保留提取信封的适配。

object_description 的 components 整项绑定与 prefab 可选描述字段均已接入反射，见后续小节。
覆盖指令继续保留专用 codec，不将文件协议处理混入通用字段遍历。

本批仅完成共享入口及字段约束核查，验收见
[记录](../records/2026-11-04-component-dom-codec.md)。

### 非标量属性整项绑定与 components 注册（已完成）

- getter 返回稳定 const T&，setter 接收完整 const T&，可返回 error 或 void。
- 非标量绑定不开放可写地址或子元素视图。读取走 const 视图，整项赋值走 setter。
- JSON 解码创建独立默认候选，完整解码成功后才调用 setter；setter 须保证失败不改变业务对象。
- components 注册为向量属性，统一由 component_descriptions::replace 校验非空/唯一后替换。
  普通对象 codec 删除手动组件数组读写，仅保留普通对象必需 components 与 prefab 互斥校验。
- prefab 字段表达不在本批范围；保留其已有包装与覆盖冲突检查。

验收覆盖容器与嵌套对象的成功替换、拒绝旁路修改、解码/校验失败保持原值、类型错误、
Scene/Prefab 往返与编辑器撤销。受影响 Debug 目标编译及 18 项相关测试通过，见
[验收记录](../records/2026-11-04-bound-object-properties.md)。未运行大场景或远端 CI。


### 可选值反射恢复

用户确认恢复 optional 的独立类别、操作表和借用视图，以支持真实的可选描述值。
保持 parent_uuid 的 nil GUID 表达，不恢复可选父 ID，也不改变 ref 的所有权。
实现范围为 Meta 类型访问和 JSON 往返；prefab 字段注册及覆盖指令适配仍是后续任务。
API 与空值歧义处理见 [反射参考](../reference/reflection.md#可选值反射)。

已完成基础能力与相关验证，见[验收记录](../records/2026-11-05-meta-optional.md)。


### Prefab 引用字段反射接入

已注册 prefab_reference_description 三个字段和 object_description.prefab optional 属性。
覆盖指令保持专用 codec，URI、重复覆盖和结构/属性覆盖冲突集中在描述校验器。
普通对象写 null，缺失或 null 都表示无 Prefab；编辑器嵌套模板检测同步识别空值。
本批不包含 optional 面板控件或覆盖指令格式重设计。

本批受影响目标编译和 6 项测试通过，见[验收记录](../records/2026-11-05-prefab-reference-reflection.md)。


### 对象通用编解码与校验归位

删除 object_description 的 encode_object、decode_object、parse_object 包装，使用现有
serialization + validate 注册。对象校验只检查 UUID 非空和 Prefab/组件互斥；
引用描述继续检查 URI 与覆盖冲突，层级规则保留场景整体校验。
components 缺失使用空列表，保存始终输出，移除按 Prefab 存在性进行的字段省略。

已完成，相关构建和 6 项测试通过，见[验收记录](../records/2026-11-05-object-reflected-codec.md)。


### 材质与输入映射 codec 收敛

删除 material_codec 与 action_map_codec 的头文件和实现，工具、编辑器与测试统一使用 Meta。
资源层统一格式注册辅助替代两种资源各自的 encode/decode 转发，保留原格式约束及校验。
构建清单、内部头文件白名单和相关边界检查同步移除已删除路径。

已完成相关构建、8 项测试和分层检查，见[验收记录](../records/2026-11-05-resource-codec-consolidation.md)。


### Scene/Prefab 格式包装统一

用户确认允许版本错误码的小幅行为调整。采用统一正整数版本规则，删除重复场景/Prefab
编解码与中间 decoded_object_document，整体校验独立为 scene_validation。
清除无生产消费者的 Prefab dependencies 和 source_schema_version；复用已有运行时组件依赖收集。
字符串调用迁移为通用 Meta 文本入口，统一序列化诊断。旧 prefab_instances 仍明确拒绝。

已完成，Windows Clang Debug 全量构建、19 项相关测试与分层检查通过，见
[验收记录](../records/2026-11-05-scene-prefab-codec-consolidation.md)。


### Meta 默认注册表入口收敛

补齐运行期类型视图、枚举及属性绑定查询的无 registry 入口，迁移 Meta 外部直接取表的业务调用。
视图编解码复用已绑定注册表，内部递归与 codec 回调继续显式传递上下文。
边界检查禁止业务源码直接调用 default_registry；注册表机制测试保留例外。
验收覆盖默认表视图创建、隔离表编解码、场景与编辑器调用以及边界反例。

已完成，全量编译、15 项相关测试与边界反例检查通过，见
[验收记录](../records/2026-11-05-meta-registry-entrypoints.md)。


### 组件集合序列反射

component_descriptions 接入 sequence_traits，提供只读元素访问与整组校验替换。
通用序列解码增加可选 replace 能力：解码候选 vector 后再提交，不开放集合可写元素。
对象 components 改为直接成员注册；IPC 和 Prefab 覆盖使用通用视图 DOM 编解码，
删除 read_components/write_components。验收覆盖多态往返、空项/重复项拒绝及失败保留旧值。

已完成，全量编译、17 项相关测试和分层检查通过，见
[验收记录](../records/2026-11-05-component-sequence-reflection.md)。


### Prefab 组件覆盖反射

按用户确认调整未发布格式：source_node_uuid、type_guid、added 均必填，删除使用 added: null。
直接注册成员并保留局部校验，删除组件覆盖专用 codec；不提供旧格式兼容。
已完成，Windows Clang Debug 构建、8 项相关测试及分层检查通过，见
[验收记录](../records/2026-11-05-component-override-reflection.md)。

### Variant 与 Prefab 属性覆盖

已确认：为 std::variant 增加独立 ops/view，文件标签由显式 traits 配置，使用 $case/$value。
Prefab 属性覆盖直接注册 key/value，路径复用 variant；engine::value 保留既有基础值格式。
删除重复属性覆盖 codec，迁移未发布格式，不提供旧格式兼容。
验收覆盖标签往返、分支重排、非法标签、未配置标签、非默认构造分支、嵌套路径及场景/编辑器调用。

已完成，构建与相关测试通过，见[验收记录](../records/2026-11-05-meta-variant.md)。

### JSON 节点编解码收敛

用户选择优先处理 JSON 中间转换。资源格式包装和 value 使用节点 codec，文件边界只解析/输出一次。
保留原始格式、严格字段检查、校验与错误路径；字段遍历借用当前注册表和递归上下文。
纹理独立文件入口复用节点字段访问；删除不再使用的 serialize_reflected 字节入口。
验收覆盖独立注册表、嵌套回调深度、文件大小限制、格式头及资源与编辑器调用。
本项不调整 value 注册归属或覆盖校验分层。

已完成，构建与 21 项相关测试通过，见[验收记录](../records/2026-11-05-json-node-codecs.md)。

### Meta JSON 适配边界

按用户确认收回 value 注册及标签编解码，删除对外 detail 头。
节点 codec 通过借用上下文遍历字段，IPC 通过明确的内部 JSON 适配入口调用；
不新增 JSON DOM 抽象，不改变文件格式或递归校验规则。

已完成，Windows Clang Debug 构建及 25 项相关测试通过，见
[验收记录](../records/2026-11-05-meta-json-boundary.md)。

### Value 对象强引用

用户确认增加受约束的对象强引用分支，复用现有 ref 和 meta::object。
已确认规则见 [ADR-104](../decisions/ADR-104-value-object-reference.md)。
实施 value 存储及引用属性读写，保留标量快照和持久化边界；
验收共享生命周期、类型检查、const 保护和原有序列化回归。

已完成，Windows Clang Debug 构建及 28 项相关测试通过，见
[验收记录](../records/2026-11-05-value-object-reference.md)。
