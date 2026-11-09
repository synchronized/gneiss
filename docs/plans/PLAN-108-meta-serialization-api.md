<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108：Meta 访问 API 与序列化策略收敛

## 状态与范围

2026-11-09：实施中，属性范围、句柄、实例门面及旧查询清理已完成；注册适配与访问边界已完成；策略和上下文尚未完成。
新增句柄布局、属性局部 builder 与动态类型门面整理，方向已同意，实施待开始；
查询失败和冻结后结构类型解析的取舍见第二节补充，不将候选方案写成现有行为。
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

现有计划中的可失败解析均返回 result。新增 dynamic_type 便利门面是否允许无效句柄是下文
列出的待确认例外；确认前保持当前错误契约，不提前推广到其他查询接口。
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

### 句柄与实例布局补充（方向已同意，尚未实施）

源码核查：type 目前重复保存 registry、access、id、guid、kind、scalar_kind 和 enumeration；
const_instance 同样重复携带类型信息，可写 instance 又在完整只读视图外保存 data/parent。
property 已经是 registry + record，但 record 使用 const void*。各容器 view 保存完整实例视图，
因此应优先缩小实例，而非为每种容器另造轻量所有权协议。

目标结构示意：

```cpp
class type {
  registry_ref registry_;
  const detail::type_record* record_{};
};
class property {
  registry_ref registry_;
  const detail::property_record* record_{};
};
// 两种模式互斥：可寻址对象/成员，或者必须经过 getter/setter 的属性位置。
template<bool Mutable> class basic_instance {
  meta::type type_;
  // location：对象地址，或父对象地址 + 冻结 property_record。
  // 用明确的 tagged representation，不能把 getter 临时结果当稳定地址。
  detail::instance_location<Mutable> location_;
};
using const_instance = basic_instance<false>;
using instance = basic_instance<true>;
```

类型身份、类别、标量类别与 Ops 从唯一类型记录取得；record 指针在句柄存活期间稳定。
统一记录必须覆盖对象、枚举、标量和 sequence/associative/optional/reference/variant，
不能只替换现有对象记录指针而丢失其他类型能力。分类 Ops 继续复用现有静态表。
只读转换借用同一位置，不再额外保存一组可写/只读指针；setter 约束、只读传播、基类地址调整、
失败不发布 getter 临时对象等契约保持。首版不增加属性代理的隐式嵌套写回。

继续使用 registry_ref 保活注册表，不改为裸 registry 指针，不让实例视图保活业务对象。
property_range 保活表并借用稳定范围；迭代器是否持有共享所有者先保持当前安全语义，
不为了减少引用计数改变迭代器寿命契约。sizeof 仅记录目标平台实测值，不写跨平台固定字节承诺。

动态类型的目标门面为返回反射类型而非仅返回进程 type_id：

```cpp
meta::type object::dynamic_type() const noexcept;
meta::type object::dynamic_type(const registry_ref&) const noexcept;
```

内部保留实际 C++ 类型身份查询钩子，门面通过所选注册表解析；对象不新增 registry/type 成员，
不隐式注册或冻结。dynamic_guid 可由解析后的 type.guid 代替；dynamic_address 继续承担多重继承
下完整对象地址取得。持久化使用调用上下文的注册表，不能通过默认门面绕过显式 registry。
上述返回形式尚受下面决策 A 约束；必须同步核对 value 对象引用和 reference_ops 的调用。

### 需要确认的取舍与推荐

以下仅阻塞新增布局子阶段的相关实现，不阻塞已经确认的领域校验拆分。

| 决策 | 推荐方案 | 优点 | 代价/缺点 | 备选方案及代价 |
| --- | --- | --- | --- | --- |
| A：dynamic_type 查询失败 | 按用户希望提供直接 type 门面；未注册/表未就绪返回无效 type，作为明确的便利查询例外。需要精确诊断的序列化仍用上下文 registry + 实际 type_id 的 result 查询 | 对象侧调用简短；不保存重复身份；错误敏感路径仍保留 not_found/not_ready 等信息 | 便利门面不能区分失败原因；与现有统一 result 规则存在例外，必须确认并写明 | 全部返回 result<type>：错误统一，但访问链需要解包；仅允许已注册对象并断言：调用简洁，却限制对象在初始化前存在，不推荐 |
| B：冻结后首次访问未预登记的结构类型 | 显式注册元数据仍冻结；允许内部惰性归一化不可变类型记录。已知字段类型在 freeze 预建，后续仅为结构类型补记录；对象/枚举叶子仍须已注册 | 保留 type_of<vector<T>> 等便利用法；句柄始终只有 registry + record；不改变业务元数据 | 首次解析可能分配/加锁/失败；缓存随 registry 存活；须处理递归类型、并发首次解析与失败清理 | 所有类型 freeze 前显式登记：实现和只读语义简单，但每种容器/包装组合都要登记，属于使用方式变化；在 type 中另带 ops/身份则偏离本次精简目标 |
| C：类型句柄相等语义（如提供比较） | 使用 registry 身份 + record 身份；跨表比较 C++ 类型显式使用 id，比较持久类型使用 guid | 不把两个不同策略/字段集合的注册表句柄误认为同一反射定义 | 跨表同一 C++ 类型的句柄不相等；调用方需明确比较目的 | 只按 type_id：跨表方便，但忽略注册定义；只按 guid：无法覆盖无持久身份的运行类型，不推荐 |

B 的缓存不开放运行期注册、修改属性或替换 Ops，不改变本计划的运行期动态注册非目标。
实现前先固定 A/B 的行为；C 可随句柄比较接口一并确认，不为了比较额外增加全局类型表。
已明确无需再次讨论的边界：保活 registry、借用业务对象、保留 getter/setter 访问位置、
不增加插件卸载或任意 C++ 对象拥有容器、不改变文件格式。

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

### 属性局部 metadata builder（方向已同意，尚未实施）

借鉴 Caors 的属性局部绑定形式，保留本项目类型化扩展，不复制字符串键元数据系统：

```cpp
auto status = meta::registration<camera_settings>{}
    .property<&camera_settings::fov>(meta::property_id{1}, "fov")(
        serialization::field_policy{.required = true},
        property_edit_policy{.read_only = true})
    .property<&camera_settings::near>(meta::property_id{2}, "near")(
        serialization::field_policy{.required = true})
    .commit();
```

property 返回 property_builder，其 operator()(extensions...) 只修改该属性，并返回可继续
注册/提交的 builder。未附加扩展时也可直接继续 property/commit；不能要求空括号作为结束符。
代理保存 builder 借用和稳定属性索引，不保存可能因 entries 扩容失效的指针；不得超过父 builder
寿命使用。注册器不依赖“最近一次属性”的隐式状态；临时注册器链必须在完整表达式内安全工作。
同类扩展重复仍报错，失败由 commit 统一报告，不在代理析构时提交。类型级 metadata 保持显式。

收益是属性与其策略相邻、无需重复 property_id；代价是增加小型注册代理和链式转发实现。
迁移所有 property_metadata 调用后删除旧重复指定 ID 的便利接口，不增加两套元数据存储。

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
4a. [ ] 句柄布局补充：确认 A/B，统一稳定 type_record，覆盖全部类型类别与无 GUID 类型；
   type/property 收敛为 registry + 强类型记录指针，按 C 定义需要的比较行为。
4b. [ ] 实例轻量化：统一只读/可写访问位置，精简容器视图；迁移动态类型门面，删除重复身份查询。
4c. [ ] 属性局部 builder：支持 property(...)(extensions...)，迁移并删除 property_metadata 旧入口。
5. [ ] 策略拆分：替换 binding；先迁移已有 codec/引用，再做字段覆盖；
   迁移 editor_read_only/enum_name，分类迁移 validate，删除旧 flags 和 reflected 开关。
6. [ ] 序列化入口：read/write contexts 和私有操作状态，统一 encode/decode/read_into/write；
   JSON、特殊 codec、引用收集/绑定全部迁移，删除旧入口，不新增阻塞解析。
7. [ ] 快照及领域接入：capture/apply/restore 使用新句柄和策略；验证 value 不共享可变历史状态；
   接入资产、场景/Prefab、IPC、编辑器、导入器和示例，不新增平行规则。
8. [ ] 布局与收尾：移动文件、更新接口文档、删除旧头/接口/冗余类型；完成定向 CPU 验收并记录真实剩余限制。

阶段 4 原有完成状态不代表新增 4a–4c 已完成；新增步骤按 4a → 4b → 4c 实施，
与剩余策略及上下文工作在同一分支收敛，不建立第二条长期兼容实现。
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
required/enum_name 已迁至 field_policy，编辑限制已迁至 property_edit_policy；
Meta 改用内存访问约束 property_access，旧 flags 已删除，见
[字段策略记录](../records/2026-11-09-property-policy-separation.md)。
字段引用策略覆盖与 save 行为已接入文件遍历及快照，详见[字段覆盖记录](../records/2026-11-09-field-reference-policy.md)。
Scene/Prefab 整体校验已移至文件格式边界，文档恢复保持显式校验，见
[领域根校验记录](../records/2026-11-09-scene-domain-validation.md)。
Action Map 的领域校验已移出通用遍历，文件、输入服务与自定义解码加载统一检查，见
[输入映射校验记录](../records/2026-11-09-action-map-domain-validation.md)。
Mesh/Texture 描述校验已移到所属资源模块并接入文件边界，详见
[网格纹理校验记录](../records/2026-11-09-mesh-texture-domain-validation.md)。
局部对象、组件、数学值和其他资产校验的分类迁移尚未完成。

## 九、验收矩阵

| 范围 | 必须覆盖 |
| --- | --- |
| type/property | 冻结前后、查找失败、继承基类地址调整、getter 临时值、只读属性、范围寿命；全部类型类别使用稳定记录，跨表隔离与比较，句柄复制不复制元数据，实测布局 |
| instance/views | const 写入编译拒绝、运行只读拒绝、嵌套结构、容器元素失效约定、自定义容器 |
| 注册 | traits 身份、冲突/重号/重复注册、显式隔离注册表、默认入口、无持久 GUID 的运行类型；局部 metadata 归属、重复扩展、无括号链、临时链、扩容与 commit 失败；结构记录并发首次解析及递归类型 |
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

原阶段无需新增用户决策；新增句柄整理的 A/B 需确认，C 随比较接口确认，见第二节。
仅实现细节如稳定属性范围的容器类型可在相同契约下选择；
若涉及存档语义、引用身份或领域边界变化，先补充此计划和 ADR，不能伪装成命名整理。

完成条件：上述八阶段及新增 4a–4c 全部交付，生产调用统一到目标 API；旧 binding/旧语义入口清除；
资产和编辑历史既有行为通过定向验收；Reference 描述已实现 API，Record 记录执行结果。
本计划完成不自动等于 DEV-052 发布完成。
