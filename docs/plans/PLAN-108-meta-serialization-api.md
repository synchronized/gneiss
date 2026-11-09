<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108：Meta 访问 API 与序列化策略收敛

## 状态与范围

2026-11-09：实施中，属性范围、句柄、实例门面及旧查询清理已完成；注册适配与访问边界已完成；策略和上下文尚未完成。
归属 [DEV-052](DEV-052-architecture-cleanup.md)，长期边界见
[ADR-113](../decisions/ADR-113-meta-serialization-boundaries.md)。
本文是目标 API 与迁移步骤的权威来源；代码示意均为目标接口，不表示已实现。
不限制内部改动范围，但保留现有已确认的数据语义，不重建并行实现，不修改 Granit。

## 目标与非目标

目标：以类型/属性句柄统一查询，以 instance 统一借用访问，以显式策略统一持久化；
普通结构体注册字段即可参与遍历，格式和领域规则不进入 Meta 的内存访问实现。

非目标：Binary 后端实现、自动共享对象图协议、任意 C++ 对象的万能 value、插件卸载、
运行期动态注册、脚本绑定、资产加载模型重做、GPU/流送功能及远端发布。
Binary 作为后续消费者验证架构方向，本计划不能以不存在的 Binary 实现宣告跨格式验收。

## 现状与修正

| 现有证据 | 处理 |
| --- | --- |
| meta/type.hpp 已有 type；properties 返回 vector<property_info> | 收敛为持有注册表的属性句柄和稳定属性范围，减少重复复制及再查找 |
| meta/instance.hpp 已有 instance 和 mutable_instance | 采用从零方案命名：只读 const_instance，可写 instance；同阶段迁移调用方，禁止留下含义反转的旧别名 |
| meta/ops 与各类 *_view 已分开，detail/type_ops.hpp 以 variant 表示唯一类别 | 保留，不换成一排空指针；只完善稳定自定义适配入口 |
| registration.hpp、默认注册表、冻结机制已存在 | 保留机制，收敛 builder 和 type_of/view 便利入口 |
| serialization 已有 reader/writer、traversal、JSON、context-first | 复用遍历骨架；不另造 JSON 或动态值中转管线 |
| serialization/binding.hpp 混合对象、引用、快照、校验等职责 | 拆为明确策略，在同一注册表关联，不增加平行服务或注册表 |
| property_flags 带 editor_read_only、JSON enum_name | Meta 只解释内存访问能力，其余由 Editor/Serialization 扩展元数据解释 |
| context.hpp 混合配置与路径、深度、reader 等运行状态 | 分离读写调用上下文和每次操作的私有遍历状态 |
| serialization/value.hpp 已有 capture/apply/restore | 保留其归属及语义，使用统一属性访问和引用策略；不搬到 Meta |
| value.hpp 已有 record/sequence/map/optional/variant 及引用分支 | 保留，不为此次整理增加任意 C++ 对象存储分支 |

上述内容替代讨论中的示意：不把所有 Ops 改成可选指针，不将快照搬进 Meta，
不以新增 Binary 为本轮完成条件。采用从零 API 的命名，但已有能力复用实现。

## 一、对象与依赖结构

```text
Core 值、GUID、ref
  ↑
Meta：注册、类型/属性句柄、借用视图、分类 Ops
  ↑
Serialization：内容遍历、格式适配、引用表示、快照转换
  ↑
Resource / Scene / Editor：资源定位、业务校验、编辑提交与历史
```

Meta 不包含具体序列化 binding、JSON 类型、Editor 控件或资产服务。
扩展元数据的存储与类型查找由 Meta 提供；元数据结构和解释属于消费模块。
Serialization 不直接依赖 asset_manager；资产层实现引用解析适配并调度依赖。

## 二、类型、属性与实例 API

目标核心接口：

```cpp
result<meta::type> meta::type_of<T>();
result<meta::instance> meta::view(T& object);
result<meta::const_instance> meta::view(const T& object);
// 禁止绑定临时对象；显式注册表版本仍可用于隔离测试和工具。
```

所有可能失败的解析均返回 result，不用空句柄吞掉未注册类型错误。
以下 TRY 仅为传播 result 的伪代码，不要求项目新增宏：

```cpp
auto type = TRY(meta::type_of<camera_settings>());
auto object = TRY(meta::view(camera));
for (auto property : type.properties()) {
    auto member = TRY(property.get(object));
    inspect(member);
}
auto transform = TRY(object.member("transform"));
auto position = TRY(transform.member("position"));
auto x = TRY(position.member("x"));
TRY(x.assign(3.0f));
```

- type/property 是轻量只读句柄，保活冻结注册表，不保活业务对象。
- property 提供 id/name/type/declaring_type、可读写能力及扩展元数据查询。
- property::get(const_instance) 返回 const_instance；get(instance) 返回 instance，
  属性只读时写操作拒绝，不允许 const 目标通过任何路径变为可写。
- properties 返回带注册表寿命的只读范围；冻结时准备必要索引，不在每次遍历复制元数据数组。
- 成员查找支持 property_id 和名称；继承通过声明类型与基类调整访问，不能把偏移直接用于错误对象。
- getter 属性可以是代理视图，不承诺有地址；read/assign 调用 getter/setter，禁止借用 getter 临时值。
  对非可寻址 getter 结果的嵌套修改明确 unsupported，调用方整体读改写；首版不新增隐式写回链。
- instance 不缓存容器元素的长期地址；对象移动、销毁、容器修改后的失效规则与底层对象一致。
- 继续保留按路径解析能力，持久路径遵循已有稳定元素身份规则；不引入 XPath 字符串身份。
- 普通调用方不使用 type_info/type_definition；定义结构限注册层，内部存储限 detail。

## 三、注册与自定义 Ops

```cpp
auto status = meta::registration<camera_settings>()
    .property<&camera_settings::fov>(meta::property_id{1}, "fov")
    .property<&camera_settings::near>(meta::property_id{2}, "near")
    .property<&camera_settings::far>(meta::property_id{3}, "far")
    .commit();
```

- 字段保留显式 property_id 和编译期成员指针，不自动从顺序或名称生成身份；
  复用现有访问器，不为接收运行期成员指针增加捕获状态。
- traits 已定义名称/GUID 时直接使用，允许未定义时显式补充；冲突必须报错，禁止默默覆盖。
- 注册失败在 commit 明确返回，不依靠析构隐式提交；重复注册、重复属性 ID/名称必须有诊断。
- 默认注册表路径不暴露 default_registry 调用；显式 registry 上下文保留。
- 注册完成冻结，查询只读；句柄保活表，注册表不保活对象，不新增可卸载类型协议。
- type_id 表示进程类型身份，GUID 表示持久身份；普通临时 C++ 类型不强制 GUID，
  多态持久化与需要稳定类型标识的数据必须有 GUID，缺失时在相应操作报错。
- sequence/associative/optional/reference/variant 保持不同类别与视图。
- 自定义容器通过公开 traits/适配入口生成相同 Ops，不要求用户包含 detail/type_ops.hpp。
- 首版用一个测试自定义容器验证扩展点，保留现有内置容器支持；不预建多个无人使用的适配框架。

## 四、序列化 API 与上下文

```cpp
serialization::read_context read;
auto object = serialization::json::decode<scene_description>(read, text);

serialization::write_context write;
auto text = serialization::json::encode(write, description);

// 格式无关核心入口，失败可能部分修改目标或输出。
serialization::read_into(read, reader, target_view);
serialization::write(write, source_view, writer);
```

提供无 context 的便利重载，其内部创建默认 context；显式版本统一 context-first。
encode/decode 返回 result<T, diagnostic>；read_into/write 返回 result<void, diagnostic>。
默认策略/限制放调用上下文，路径、深度、活动引用、reader 等放每次操作私有状态。
嵌套 codec 继承同一操作状态，禁止通过新建状态绕过深度和引用环检测。
调用上下文可以顺序复用；不承诺用户 resolver 的并发安全，不默认允许同一上下文并发使用。

JSON 适配只处理语法与节点，通用遍历处理对象/容器/引用，特殊 codec 是显式例外。
内存对象直接通过 instance 遍历，不先生成 value 或 JSON DOM 再给另一后端使用。
独立 decode 构造候选并成功后返回；read_into 失败可能部分修改，不自动回滚 setter 的外部副作用。
诊断保留错误码、属性路径、类型上下文与必要格式位置；不引入异常返回协议。

## 五、内容、引用、字段策略与校验

将现有 binding 的职责分清，目标概念为 content_codec、reference_policy、field_policy；
名称可以在实施中依代码习惯微调，职责和优先级不能含糊。

- content_codec：特殊内容读写；普通类型默认反射遍历，无需单独开启 reflected。
- reference_policy：内嵌、外部资产、文档对象引用，以及是否携带动态类型信息。
- field_policy：参与保存、必填、枚举表示、字段级引用策略覆盖。
- 选择优先级：字段显式策略 > 类型默认策略 > 框架基础类型/反射默认行为。
- Editor 元数据如只读展示、范围由编辑器解释；Meta 可写性仍是不可绕过的访问约束。
- 通用语法/数值/必填/类型约束由序列化检查；场景层级、重复身份、覆盖目标等由领域 validate 调用。
  迁移旧 validate 时逐项分类，确保加载和保存入口仍执行必要业务校验，不能直接删除回调丢掉校验。

引用格式与行为：

| 情况 | 规则 |
| --- | --- |
| 内嵌普通引用 | 保存内容，空引用 null；共享引用按内容展开，不保存别名关系 |
| 显式多态内嵌引用 | 非空统一 $type_guid/$content，不依赖当前是否恰好为基类 |
| 外部资产引用 | 保存稳定定位，经资产层 resolver 绑定；内容未就绪不得写成 null |
| 文档对象引用 | 保存文档域定义的稳定地址，Meta 不解释场景身份 |
| 内嵌引用环 | 带路径报错，不自动生成 $id/$ref |

同一类型允许不同字段采用不同策略；显式不支持的组合必须报错。
通用框架不自动判断“ref 加有子类”等于多态，不通过类型名 switch 猜业务。
保留 PLAN-107 收集/绑定阶段：收集记录定位，资产调度提交依赖，绑定取得已分配身份，
content/reference 就绪语义仍由资产加载操作决定。Codec 内禁止启动轮询或等待 I/O。

## 六、value 与快照不改变的边界

- value 继续拥有标量、数学值、结构化数据树及明确引用表示；任意 C++ 结构体通过 instance 访问。
- value 的对象强引用分支复制时共享对象，不隐式深拷贝，不视为历史快照。
- capture_value 显式形成独立语义的数据树；不可变子树允许共享存储，原对象后续修改不能改变历史。
- 外部资产引用捕获身份，文档对象引用捕获稳定地址；禁止把裸指针或未明确策略的活动对象当冻结快照。
- apply_value/restore_value 保留在 Serialization，复用统一属性与引用策略，不新建 meta::capture。
- 快照上下文只携带其需要的注册表、引用策略和限制，不要求 JSON reader/writer；
  可复用内部遍历支撑，但不经 JSON 文本往返。
- 撤销、Prefab 覆盖、子树/文档快照继续消费 value；本计划不重写编辑历史算法。
- 保留 $kind/$content、GUID nil 写 JSON null、vector4 与 quaternion 区分等已确认规则。
  不为 API 重命名改存档格式；若实施发现必要变更，更新权威格式、仓库资源及测试，不新增未发布兼容层。

## 七、目标文件布局

```text
core/meta/
  type.hpp / property.hpp / instance.hpp
  registration.hpp / registry.hpp
  property_path.hpp / object.hpp
  ops/                     分类能力与公开适配契约
  views/                   容器、optional、reference、variant 视图
  detail/                  存储、句柄构造、适配生成实现
core/serialization/
  context.hpp / diagnostic.hpp
  reader.hpp / writer.hpp / serialization.hpp
  policy.hpp               内容、引用与字段策略
  json.hpp/.cpp
  value.hpp/.cpp            结构化快照入口
  detail/                  格式适配、遍历与操作状态
```

按职责形成布局，模板必需实现仍在头文件；不为隐藏模板重复造非模板接口。
Binary 后续有真实任务时新增 binary.hpp/.cpp。本轮不创建空文件或空目录。
移动视图与头文件在 API 迁移闭环后一次完成，同步 CMake、独立头检查和 include，删除旧转发头。

## 八、实施阶段与删除条件

1. [x] 基线：列出 type_info/property_info、旧 instance、binding、context、validate 的全部生产调用点；
   记录现有 JSON/快照格式和错误行为，建立成功、失败、生命周期基线。
2. [x] 属性句柄：实现 type_of、property 范围与 get；继承、getter 和只读边界验证；
   同时迁移 Inspector 与遍历，不保留第二套公共属性元数据查询。
3. [x] 实例 API：采用 instance/const_instance、view/member/assign；迁移所有调用、测试和路径解析。
   删除 mutable_instance 旧名；编译验证 const 拒绝写、临时对象拒绝借用。
4. [x] 注册与适配：builder commit、默认/显式注册上下文、traits 适配契约、冻结属性索引；
   移除普通消费代码对注册定义及 detail Ops 的依赖。
5. [ ] 策略拆分：替换 binding；先迁移已有 codec/引用，再做字段覆盖；
   迁移 editor_read_only/enum_name，分类迁移 validate，删除旧 flags 和 reflected 开关。
6. [ ] 序列化入口：read/write contexts 和私有操作状态，统一 encode/decode/read_into/write；
   JSON、特殊 codec、引用收集/绑定全部迁移，删除旧入口，不新增阻塞解析。
7. [ ] 快照及领域接入：capture/apply/restore 使用新句柄和策略；验证 value 不共享可变历史状态；
   接入资产、场景/Prefab、IPC、编辑器、导入器和示例，不新增平行规则。
8. [ ] 布局与收尾：移动文件、更新接口文档、删除旧头/接口/冗余类型；完成定向 CPU 验收并记录真实剩余限制。

每阶段交付可编译闭环并本地提交，同一特性分支推进，不按子步骤创建 PR。
迁移期间允许短暂内部适配，但该阶段结束须移除；禁止以长期兼容别名掩盖读写语义变化。

### 首批迁移状态

已替换 type/instance 的复制属性列表返回为 property_range，并接入 property::get。
JSON 遍历、value 捕获、Inspector、Prefab 路径查询使用新范围。
阶段 2 已完成：生产、测试与示例统一使用 type/property 句柄，
describe_type/describe_property、type_info/property_info 已删除；
枚举通过 type_at，版本通过 type::schema_version 查询，getter/setter 校验保留。
默认构建及 40 项 CPU 回归见[旧查询删除记录](../records/2026-11-09-meta-query-removal.md)。
阶段 3 已迁移 instance/const_instance、view/member/assign，默认构建和 35 项 CPU 回归通过，
见[实例门面迁移记录](../records/2026-11-09-meta-instance-api.md)；阶段 4 已统一 registration<T>、显式身份设置并验证公开 sequence_traits；
冻结属性索引已接入，见[索引记录](../records/2026-11-09-meta-frozen-index.md)；
无 GUID 对象注册、嵌套访问及持久化身份边界已实现，见
[运行期身份记录](../records/2026-11-09-meta-runtime-identity.md)；序列化已改用正式实例与引用视图接口，两个私有访问辅助头已删除，见[访问边界记录](../records/2026-11-09-meta-access-boundary.md)。验证见
[注册入口记录](../records/2026-11-09-meta-registration.md)。调用点审计与验证见
[首批记录](../records/2026-11-09-meta-property-handles.md)。

阶段 5 已加入类型/字段扩展元数据基础，见[实现记录](../records/2026-11-09-meta-extension-metadata.md)；
binding 存储和查询已迁到扩展/Serialization，见[存储迁移记录](../records/2026-11-09-serialization-policy-storage.md)；
旧 binding、注册快捷方法及 reflected 开关已删除，默认字段遍历和消费调用已迁移，
见[策略拆分记录](../records/2026-11-09-serialization-policy-split.md)。
字段策略覆盖、flags 清理及领域校验分类尚未完成。

## 九、验收矩阵

| 范围 | 必须覆盖 |
| --- | --- |
| type/property | 冻结前后、查找失败、继承基类地址调整、getter 临时值、只读属性、范围寿命 |
| instance/views | const 写入编译拒绝、运行只读拒绝、嵌套结构、容器元素失效约定、自定义容器 |
| 注册 | traits 身份、冲突/重号/重复注册、显式隔离注册表、默认入口、无持久 GUID 的运行类型 |
| 策略 | 同类型字段不同引用方式、类型默认与字段覆盖、未知动态类型、未注册/缺 GUID、空引用与引用环 |
| 序列化 | 普通字段零自定义 codec、特殊 codec、深度限制、错误路径、context 复用、decode 失败不暴露候选 |
| 资产 | 收集不暴露临时结果、绑定不等待 I/O、内容未就绪仍保存身份、缺失依赖与环保持既有语义 |
| value/历史 | 对象引用共享、显式捕获隔离、不可变子树共享、撤销重做与 Prefab 覆盖恢复 |
| 格式 | $kind/$content、$type_guid/$content、GUID null、枚举、数学类型区分及样例往返 |
| 边界 | Meta 无 Serialization/Editor/Asset 具体依赖；普通消费者不访问 detail；独立头编译 |

阶段内默认编译优先，核心语义闭环运行针对性 CPU 测试，最终集中运行相关 CPU 回归。
不执行大场景、GPU、耗时远端 CI；不把未执行项目记为通过。文档阶段仅检查链接、范围和 diff。

## 十、代价、风险与完成定义

主要代价是 instance 名称语义迁移、属性代理/继承访问、策略优先级贯穿快照与资产绑定。
按依赖顺序实施，避免同时维护两套 API。属性句柄保活注册表仍不保活目标，
未赋址 getter 的嵌套修改首版明确拒绝；未知字段保留/插件缺失恢复不是本计划新增能力。

当前没有需要用户继续拍板的阻塞决策。仅实现细节如稳定属性范围的容器类型可在相同契约下选择；
若涉及存档语义、引用身份或领域边界变化，先补充此计划和 ADR，不能伪装成命名整理。

完成条件：上述八阶段全部交付，生产调用统一到目标 API；旧 binding/旧语义入口清除；
资产和编辑历史既有行为通过定向验收；Reference 描述已实现 API，Record 记录执行结果。
本计划完成不自动等于 DEV-052 发布完成。
