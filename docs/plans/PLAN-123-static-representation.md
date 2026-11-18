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
