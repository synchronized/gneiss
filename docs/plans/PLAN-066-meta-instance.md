<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-066：统一 Meta 实例视图

## 状态

已完成。

## 范围与约定

先建立 instance / mutable_instance 作为只读和可写借用入口，再扩展 optional/vector。
本轮仅支持对象和基础属性，type_kind 不声明尚不可用的容器能力。
不新增 property_view 或第二套注册表；视图持有注册表、借用对象，禁止绑定临时对象。
基础属性保存父对象和属性位置，read_value 返回自有副本，getter 临时值不被借用。
嵌套对象借用成员地址；地址失效和跨线程同步由调用方负责。

## 实施与验收

- [x] 支持按名称和 property_id 递归访问、枚举、类别判断与基础值读写。
- [x] 区分只读/可写入口，保持 setter 校验、只读属性和异常转换语义。
- [x] 现有嵌套序列化、默认值校验和子 codec 赋值共用视图入口。
- [x] 验证临时值禁止借用、getter 返回值副本、只读拒绝、注册表寿命、并发只读和隔离注册表。
- [x] Debug 完整编译及相关短测试；不运行大场景、GPU 或远端 CI。

## 验收记录

2026-10-29：Windows Clang Debug 完整编译、32 项 Meta/资源/Scene/Prefab/编辑器短测试通过。
新增实例视图测试覆盖递归访问、按名称/ID 获取、类别查询、只读边界、setter 校验、
临时对象编译期拒绝、getter 自有副本、子视图独立寿命、类型不匹配拒绝及并发只读。
既有嵌套 codec、默认值、递归限制和失败释放测试继续通过；git diff --check 通过。
本轮只交付基础值和对象视图，optional/vector 留待后续计划。不运行大场景、GPU、浏览器构建和远端 CI。
