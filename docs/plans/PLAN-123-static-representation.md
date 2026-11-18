<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-123：静态类型表示能力

状态：已完成本地实施与验收（2026-11-18）。取代 PLAN-122 的独立适配器表方案。

## 已确认决策

representation_traits<T> 定义 representation_type、encode(const T&) 与 decode(const R&)，
均返回 result。静态 representation_ops 随 ops_of<T> 生成，不依赖 serialization、JSON、context。
特化必须在首次生成类型能力前可见，并在全部编译单元保持一致；不能运行时替换。
注册表只保留字段、元数据及按 GUID 查找多态类型，不再用于查找固定表示能力。

## 实施顺序

- [x] 增加静态表示操作及受生命周期约束的实例访问入口，转换临时值保持在调用栈中。
- [x] 内置 GUID、向量、四元数与矩阵共用 traits；保持 JSON 文件表示。
- [x] 通用遍历接入，删除 adapter_registry、上下文参数及旧注册 API。
- [x] 覆盖未注册原生类型、候选/属性 setter、多态、表示环及格式回归。
- [x] 同步文档、定向构建测试与本地提交。

## 最小使用流程

用户在类型头定义 meta::representation_traits<T>，调用 json::encode_bytes/decode<T>。
上下文仍使用 Meta 注册表；已知值类型本身不要求注册，空冻结表也可用。
字段和运行时多态身份仍按原有规则注册。表示与候选只在访问回调期间有效，不能保存其借用。
编码/解码失败通过 error 返回；序列化补充路径信息，并在候选校验成功后经过目标 setter 提交。
不改变 value 的存储分支，不增加二进制后端。

## 验收记录

Clang Debug 定向构建及 8 项测试通过：serialization、serialization-archive、reflection-json、
meta-instance、meta-type-ops、scene_description、value_edit、component-description。
MSVC Release 定向构建及上述 serialization、serialization-archive、reflection-json、
meta-type-ops、component-description 共 5 项测试通过。git diff --check 通过。
未运行全量测试、静态库/安装消费者及远端矩阵；本次没有修改 C ABI、安装规则或增加后端。
无默认构造的未注册值类型、空注册表、setter 拒绝、缺省值和多态表示均由现有目标覆盖。

## 后续收敛：能力视图（已完成本地验收）

- [x] type_ops 移入 ops/ 并统一到 meta 命名空间；生成器和遍历辅助仍属 detail。
- [x] representation_view 承担同步表示读写，type 只提供可判空的能力入口。
- [x] sequence_type_view 承担无实例的序列能力查询，迁移 Prefab 路径校验。
- [x] 迁移测试、同步参考文档并完成定向验收。

视图持有 type 上下文，不持有源对象或候选。正常流程先查询能力，再调用；类型不支持时
返回空视图，空视图操作属于契约错误。表示编码传入不匹配的源类型/注册表返回 invalid_argument。
枚举、字段等通用反射查询保留在 type，不增加对应的包装层。type::element_type 暂保留
为 sequence/associative/optional/reference 的通用查询，序列消费者可使用类型视图查询。

本轮 Clang Debug 定向构建与 9 项测试通过：上一阶段 8 项加 type_registry；
MSVC Release 的 meta-instance、serialization-archive 构建及测试通过。
注册表回归同时发现并修复 void 注册在拒绝前实例化值操作表的模板错误。
旧查询/表示操作与旧 type_ops 路径均已迁移，不保留兼容别名；git diff --check 通过。
未运行全量测试、静态库/安装消费者或远端矩阵。

## 后续收敛：capability 与实例绑定（已完成本地验收）

- [x] 类型通过 capability<C>() 查询能力；序列类型视图与表示视图分别迁为 capability。
- [x] 容器能力共用绑定规则，连接 sequence/associative/optional/reference/variant 实例视图。
- [x] 保留 instance/const_instance 的编译期可写性，现有 as_* 入口复用统一绑定。
- [x] 验证能力缺失、类型/注册表不匹配、正常绑定和序列化，更新文档并本地提交。

不增加能力注册表、运行时覆盖、模块贡献列表或能力版本系统。capability 持有类型上下文；
实例 view 沿用原借用寿命。类型能力不存在时返回空 capability，绑定失败返回 result。
representation_capability 可直接构造解码候选，不强制增加实例 view。

本轮 Clang Debug 定向构建与 9 项回归通过（meta-instance、meta-type-ops、type_registry、
serialization、serialization-archive、reflection-json、scene_description、component-description、
value_edit）。MSVC Release 的 meta-instance、serialization-archive 构建、链接和测试通过。
新增绑定测试覆盖空能力、类型/注册表不匹配、可写绑定及只读返回类型；现有容器测试经
as_* 入口复用同一绑定实现。未运行全量/静态库/安装消费者及远端矩阵。

## 后续收敛：专属类型查询（已完成）

- [x] 删除 type::element_type，迁至 sequence/associative/optional/reference capability；variant 无单一元素类型。
- [x] 删除 type::enum_entries，新增 enum_capability::entries；未注册枚举有能力但无条目。
- [x] 迁移序列化、Prefab 路径、编辑器及测试，验证类型查询与枚举 UI 元数据。

元素查询要求有效 capability；枚举 entries 对空能力或未注册类型返回空范围。保留 type::is_enum
作为类别判断，持久身份、字段和元数据仍由 type 管理；不新增注册表或实例 view。

本轮 Clang Debug 的 6 项 Meta/序列化测试及 5 项编辑器测试通过；MSVC Release 的
meta-instance、serialization-archive 构建、链接和测试通过。验证覆盖未注册枚举能力、
四类元素查询、枚举条目共享与编辑器消费。未运行全量、静态库、安装消费者或远端矩阵。

### 能力依赖收敛

- [x] type.hpp 不再包含具体 capability；各调用方显式声明依赖，友元声明不引入完整定义。
- [x] type.hpp 与四个 capability 头文件分别通过 Clang C++20 独立包含检查。
- [x] 完成受影响引擎、序列化和编辑器目标构建及定向回归。

本轮 Clang Debug 的上述 11 项定向测试通过；仅调整头文件依赖，未重跑 MSVC、全量及远端矩阵。

## 对象能力与命名收敛（已完成）

调用方通过 type.capability<meta::cap::object>() 查询字段，通过
instance.capability<meta::cap::object>() 绑定只读或可写对象视图后访问 member。
保留 instance/const_instance 名称与借用语义，不改变数据所有权。

- [x] 能力统一迁入 meta::cap，枚举使用 enumeration（enum 是关键字），删除旧命名。
- [x] properties/find_property 归 cap::object，member 归 basic_object_view；迁移真实消费者。
- [x] 验证字段访问、继承、只读/受约束属性、缺失能力及序列化和编辑器链路。

未注册原生 object 类型具备对象能力但字段为空；已注册空对象同样有效。
无效或非对象类型返回空能力；绑定时检查类型上下文、地址及写入限制，错误沿用既有规则。
对象视图借用宿主，不要求继承 meta::object。字段访问失败仍返回 result。
不扩展对象构造、继承关系查询或额外注册表；object_data/assign_object 本轮保持既有存储访问职责。

本轮 Clang Debug 的 22 项定向测试、MSVC Release 的 meta-instance 与 serialization-archive
测试通过；7 个类型/能力/对象视图头文件通过独立包含检查。未运行全量、静态库及远端矩阵。

## 实例读写收敛（已完成）

字段实例在 member 定位时绑定冻结访问器和调整后的宿主地址，read_value/assign/replace
直接执行值操作或 getter/setter，不再按父类型和字段 ID 重查注册表。
绑定指针由实例中的注册表引用保活，不复制访问器；用它替换 parent_type/property 成员。
只读、继承地址调整、候选整体替换及业务错误传播保持不变。

- [x] 完成实例绑定及读写实现，删除注册表执行读写的职责。
- [x] 迁移业务和测试，删除 read_property/write_property 及不再需要的访问器查询入口。
- [x] 验证动态描述、继承、受约束 setter、错误传播、快照与元数据生命周期。

动态描述允许通过 from_object(type, address) 创建类型擦除实例；支持没有 C++ 类型 ID
但有注册字段的对象描述。类型与地址匹配、宿主生命周期仍由调用方保证。

本轮 Clang Debug 构建及 16 项定向测试通过，覆盖属性访问契约、动态注册、实例、
JSON/归档序列化、Prefab 覆盖、World 与编辑器资产编辑。instance/property/registry
三个头文件通过独立包含检查。未运行本轮 MSVC、全量及远端矩阵。

## 注册表职责收敛（已完成）

已确认：注册表只承担发布、身份查找与注册类型遍历；字段定位由 property/object view 共用，
对象能力承接构造与继承操作。普通类型不强制继承 meta::object。

- [x] 删除自由 find_property，并统一 property::get 与 object view 的字段绑定。
- [x] 注册声明与描述归注册侧；删除分阶段 bind_property，迁移为原子提交。
- [x] 对象能力封装构造与继承，迁移序列化与资产解码，删除 find_object_ops。
- [x] registered_types 范围替代 type_count/type_at，保留原有枚举范围与排序。
- [x] 迁移文档、验证真实消费者及头文件依赖，删除旧路径。

构造优先复用现有 ref<void> 所有者；能力句柄提供类型上下文，借用不能超过所有者寿命。
不为统一外观新增通用 any 所有者。每组完成后记录验证，不能将局部完成标记为全部完成。

本轮 Clang Debug 构建和 13 项定向测试通过；补充类型范围/迭代器保活、只读基类转换、
跨注册表拒绝后，相关 2 项测试再次通过。6 个注册/类型描述/属性/对象能力头文件独立包含通过。
删除旧接口后源码检索无剩余调用；未运行 MSVC、全量测试及远端矩阵。

## 无可恢复失败的返回值收敛（实施中）

- [x] create_registry/default_registry 直接返回 registry_ref；分配异常继续传播。
- [x] is_frozen 返回 bool，空表属于调用契约错误；外部可恢复边界自行检查空表。
- [ ] 静态 view/from_access 直接返回借用实例，迁移消费者与测试中的冗余错误分支。
- [x] 验证注册表返回值迁移涉及的注册、冻结、实例与序列化消费者，更新参考文档。

动态类型查找、字段路径、能力绑定与业务校验仍保留各自真实失败语义。
表示转换 traits 的普通返回值支持另行处理，不与本轮静态借用迁移混合。

注册表返回值迁移已完成：Clang Debug 相关目标构建和 13 项定向测试通过，
包含场景描述与编辑器属性面板。未运行 MSVC、完整测试及远端矩阵。
静态 view/from_access 的返回值与消费者迁移留作下一组，尚未完成。
