<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CAORS 私有适配模块与强类型输入映射验证

## 范围

从测试程序抽取异步文件、VFS 后端和 worker executor 到
`src/engine/platform/caors`，各实现位于独立 `.cpp`。
`adapter.hpp` 仅供内部宿主使用，不安装、不导出；构建目标为 `gneiss_caors_adapter`。
仍由默认关闭的 `GNEISS_BUILD_CAORS_PROBE` 启用，Engine DLL 与 Editor 尚未链接它。

输入映射解析拆为 `action_map_parser.cpp`，旧 `action_map.cpp` 保留 VFS 读取包装。
新旧路径复用同一解析及语义验证，CAORS 缓存现在保存 `action_map`，不再只保存 JSON DOM。
InputService 的加载与应用入口未切换，不能据此宣称完成生产资产迁移。

## 验证

本地 Windows / Clang / Debug，警告视为错误。
私有静态库、Engine DLL 和输入映射测试构建通过；相关 11 项测试通过，接入测试重复 20 次通过。
检查真实输入映射的动作名称、物理键值、绑定缩放值，以及共享加载、缓存命中和取消。
增加合法 JSON 但键值越界的重载用例，确认语义失败保留旧缓存。
原输入映射测试继续检查重复名称、键值越界、非法缩放及失败不修改原数据。

构建过程中发现本地 CAORS checkout 已更新到不同提交，随后改用固定提交
`cbe41b75223f52bec746cc23346b28eba90fa693` 的 git archive 独立源码快照重新配置和验证，
没有修改上游工作区，也未升级依赖。快照仅是本地 FetchContent 源码覆盖，未加入版本控制。

## 后续

下一步接入资源服务与 InputService 的显式应用入口，处理请求取消、宿主退出和映射生效顺序。
Prefab 仍保留来源修订、不可变版本及回滚流程，不能直接用 GUID 缓存替换这些语义。
Linux、跨 DLL、大资源性能与错误注入仍未覆盖。
