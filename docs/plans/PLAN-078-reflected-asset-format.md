<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-078：反射资源格式包装与二维向量

状态：已完成。

## 范围

- Material、Action Map 共用资源文件外层 format/version、重复格式头与字节限制处理。
- 保留各自字段注册、校验器、版本错误码及文件布局；不迁移 Scene/Prefab 的专用结构处理。
- vector2 加入 value、类型身份、反射读写与 JSON 数组编码；保留已有 value 类别编号。
- 补齐向量在 Prefab 覆盖文件中的数值校验和表示；面板提供双分量编辑。
- 旧 Runtime IPC 尚无 vec2/vec4 类型，明确返回 unsupported；不扩展 IPC 协议。
- 核查资源引用，暂不增加异步加载型反序列化、对象图、任意 variant 或更多容器。

## 验收

完整 Windows Clang Debug 编译；格式头错误、重复键、版本与字节上限回归；vector2
往返、属性读写、分量数量/类型/溢出/非有限值失败路径；相关 Prefab 和面板回归。
不运行大场景、GPU 窗口、浏览器或远端 CI。

## 资源引用核查

material_description 通过 texture_uris 保存依赖；mesh_renderer_description 保存 mesh_uri/
material_uri；texture_description 以 URI 关联负载。运行时资源强引用负责保活，文件解析与
依赖加载由既有资源流程分别处理。目前没有需要通用 asset_ref<T> 文件编解码的描述字段。
因此本轮不新增资源引用解析协议；矩阵、其他容器与多态指针待实际字段需求出现后再补。

## 验证结果

2026-10-30：Windows Clang Debug 完整编译通过，16 项相关 CPU 测试全部通过，覆盖
reflection-json、meta-instance、meta-container、value、Action Map、Material、Scene/Prefab、
Runtime 属性转换、编辑器面板和分层/内部头文件边界。未运行大场景、GPU、浏览器或远端 CI。
