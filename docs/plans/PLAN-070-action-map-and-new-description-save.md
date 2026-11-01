<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-070：ActionMap 反射编解码与新建描述保存

## 状态

已完成（2026-10-30）。

## 范围

ActionMap v1 原生字段直接注册反射，补齐编码；保留格式头、输入输出 1 MiB 上限、
动作名唯一性和按键/scale 校验，不改文件布局。使用默认注册表。
Scene/Prefab 共用保存入口允许空 author_json，新建描述从空对象列表生成文件；
已有原文件继续合并未知字段，格式、对象树和动态组件规则保持不变。
不新增文件 DTO、缓存或通用格式框架。

## 验收

- [x] ActionMap 反射字段与编码入口，往返、非法字段和语义、长度上限测试。
- [x] 新建 Prefab 可保存，非法根拒绝；已有文件未知字段仍保留。
- [x] 完整 Debug 编译和相关短测试；不运行大场景、GPU、浏览器及远端 CI。

## 验收结果

Windows Clang Debug 完整构建通过；ActionMap、Scene/Prefab、默认注册表、资产加载和
编辑器相关短测试 20/20 通过。改动行格式化及 `git diff --check` 通过。
未运行大场景、GPU、浏览器、clang-tidy 或远端 CI。
