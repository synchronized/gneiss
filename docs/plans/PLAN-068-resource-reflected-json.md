<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-068：资源描述复用反射 JSON

## 状态

已完成（2026-10-29），按用户确认直接序列化原生描述的反射字段，不保存原始内存字节。

## 范围和已确认决策

材质升级为 v6，根为 parameters 与 texture_uris；不保留 v5 及以下兼容。
直接注册 material_description、material_parameters、texture_sampling，不维护额外文件对象模型。
补齐 std::array、vec4、原生窄整数/枚举；value 只增加 vec4 标量，不承载容器。
格式头、业务校验保留，缓存、RID、author_json 和派生依赖不保存。
Scene/Prefab 直接反射原生普通字段，继续保留组件动态类型、Prefab 覆盖和编辑器未知字段处理。
现有 Scene/Prefab 外部字段布局不变，不重写文件版本。
自定义 codec 可接收调用方注册表，材质包装器复用该表；无参材质入口要求初始化默认表。
Scene/Prefab 既有格式入口和动态组件辅助仍使用默认注册表。
离线导入 SDK 保持不链接 Engine 的边界，生成 v6 文件，不引入第二张全局注册表。

## 实施与验收

- [x] Codec 传递注册表，格式包装器可直接调用本类型反射字段读写。
- [x] 材质直接绑定原生成员，删除文件 DTO 与重复字段解析/映射。
- [x] Scene/Prefab 普通字段使用原生反射，保留未知字段与动态组件适配。
- [x] 同步仓库资产、导入器、脚本和测试夹具至 v6。
- [x] 完整 Debug 构建、资源/Meta/Scene/Prefab/编辑器/导入工具相关短测试。

## 验证范围

验证固定数组长度、vec4、整数溢出、枚举范围、重复/未知字段、旧版本拒绝、往返与失败不发布，
以及对象树校验和未知字段保留。不运行大场景、GPU、浏览器或远端 CI。

## 验收结果

Windows Clang Debug 完整构建通过；Meta、资源、Scene/Prefab、编辑器和导入工具相关短测试
44/44 通过。改动行格式化和 `git diff --check` 通过。未运行大场景、GPU、浏览器、
clang-tidy 或远端 CI。仓库材质和生成器已迁移，外部旧材质需要转换为 v6。
