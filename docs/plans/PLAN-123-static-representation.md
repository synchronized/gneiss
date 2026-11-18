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
