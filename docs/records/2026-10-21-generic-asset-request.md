<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 通用资源请求迁移验收

日期：2026-10-21。对应 [PLAN-051](../plans/PLAN-051-module-layout.md) 与
[ADR-069](../decisions/ADR-069-resource-runtime-boundaries.md)。

## 已完成

- 使用移动所有的 `asset_request<T>` 包装 CAORS 原生请求，删除输入专用虚接口与适配工厂。
- 不复制加载状态机；成功取出强引用后消费请求，失败保留原生错误且不修改输出。
- 移动赋值取消被替换订阅；析构取消本订阅，不影响独立订阅。
- `action_map_asset` 独立放入 resource/types，Runtime 取得资源后复制映射并交给 InputService。
- 原 CPU 准备描述改名 `asset_preparation_source`，避免与请求所有者混淆。
- 安装验收脚本补充复制 examples/support，修复独立示例缺少 scope_exit.hpp 的构建失败。

## 本地验证

- Windows Clang Debug 完整构建通过，相关 15 个测试通过，覆盖输入启动、CAORS 请求、VFS、
  材质文档、资产准备、纹理加载、场景/Prefab 准备与模块边界。
- 新增通用请求测试使用不同于 action_map 的资源类型；覆盖空请求、失败、移动、取消、强引用与一次消费。
- CAORS 探针覆盖真实在途订阅的移动覆盖、析构取消与独立订阅保留。
  首次测试误用了已缓存资源（立即完成），调整为新资源身份确保在途状态后通过。
- 无任务线程的静态构建与 application/asset-request 两项测试通过。
- Emscripten 构建与 Node/MEMFS 下 application/asset-request 两项测试通过。
- Windows 共享构建 installed_stable_runtime_consumer 通过；静态安装包四项消费者测试通过，
  包括 C、C++、application_world 与 property_inspector。
- 安装测试首次因未复制示例辅助头失败，补齐脚本后重跑通过。

验证日志位于本地 build/asset-request-*.log，不作为版本资产提交。

## 范围限制

未运行浏览器完整编辑器验收。此次只迁移输入请求并建立可复用模板，
不表示材质、Mesh 等全部资源路径或同步缓存发布已收敛。公共 ABI 未增加 CAORS 类型。
