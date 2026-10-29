<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-067：反射容器视图与 JSON

## 状态

已完成；接口规则见[反射容器视图](../reference/reflection-containers.md)。

## 已确认决策

value 保持标量快照；容器 read_value 返回 unsupported。instance 借用原容器，
元素继续返回 instance；不建立动态容器副本或第二套注册表。
支持 vector（排除 bool 代理）、optional、字符串键 map/unordered_map，元素可递归组合。
容器无需稳定 GUID，成员元数据保留运行期类型和结构信息；对象叶子仍需注册稳定 GUID。
只注册直接成员，返回容器临时值的 getter 不提供借用访问。

JSON 使用数组、对象及 null 表示对应容器；缺失字段保留默认值，显式输入替换容器内容。
解码仅修改新建临时根对象，失败不发布。写视图不提供事务保证。
容器结构修改后重新获取元素视图，调用方负责对象寿命和线程同步。

## 非目标

不支持任意键、set、vector<bool>、直接嵌套 optional、动态脚本值、自动元素寿命跟踪或大场景验证。

## 实施与验收

- [x] 注册类型操作表，冻结时检查对象元素注册。
- [x] 顺序、关联、optional 只读与可写视图；支持嵌套访问。
- [x] JSON 往返、重复键拒绝、诊断路径、默认值与深度限制。
- [x] 生命周期、只读拒绝、错误输入与线程边界短测试。
- [x] 完整 Debug 编译、相关短测试及文档。

## 验收记录

2026-10-29：Windows Clang Debug 完整构建及 33 项相关短测试通过。
新增测试覆盖嵌套容器读写、对象叶子漏注册、只读边界、标量校验、JSON 往返、
确定性键顺序、自定义 codec、重复键/错误路径、深度限制、失败析构、注册表寿命及同步只读。
既有 64 层对象深度边界保持通过。未运行大场景、GPU、浏览器及远端 CI。
