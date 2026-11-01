<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-079：反射类型视图与枚举面板

状态：已完成

## 目标与边界

增加引用现有注册表和静态操作表的 meta::type，不增加另一份类型数据库。
instance 返回类型视图，属性面板根据声明类型获取枚举项，覆盖容器枚举元素。
保留 type_info 快照、现有标量存储和序列化格式；暂不增加枚举稳定 GUID、全类型名称
登记、任意类型动态构造和通用 UI 元数据系统。

## 实施与验收

- 类型视图支持类别、运行期身份、存储类别、枚举项、对象属性和容器元素类型。
- 枚举查询遵守冻结和借用生命周期边界，不将未知枚举降级成整数编辑。
- 属性面板直接接收类型视图，不从父属性额外传递枚举选项。
- 验证枚举属性与容器元素共享选项、错误边界、面板绘制及序列化回归。
- 完整 Debug 编译和相关 CPU 测试；不运行大场景、GPU、浏览器和远端 CI。

## 验证结果

2026-10-30：Windows Clang Debug 完整编译通过，16 项相关 CPU 测试全部通过，覆盖
reflection-json、meta-instance、meta-container、value、Action Map、Material、Scene/Prefab、
Runtime 属性转换、编辑器面板和分层/内部头文件边界。未运行大场景、GPU、浏览器或远端 CI。
