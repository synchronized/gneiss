<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CAORS CPU 核心与平台 I/O 分离

## 改动

删除材质、Mesh 的 CMake 开关及关闭实现。CPU 缓存始终使用 CAORS；核心装配不依赖 libuv，
原生链接共享库、Emscripten 链接静态库。直接 I/O 探针库只在启用探针时构建。
输入开关暂保留；同步 acquire 与未注入缓存的底层通道也尚未合并。

完整 Gneiss Web 构建发现 component_reflection.cpp 内部 bind 与 std::bind 的 ADL 冲突，
已重命名为 bind_component_field；不改变注册语义或公共 API。
安装消费测试在 Emscripten 下把资产嵌入虚拟文件系统，避免 Windows 盘符被当成 WASM 相对路径。
原生测试仍使用原目录，未删除测试或降低断言。

## 验证环境

- Windows Clang Debug：相关 5 项集成测试通过；反射命名修复后 Editor 资产编辑、
  隐藏冒烟与资产通道 3 项再次通过。
- 无线程静态 Engine、常规共享 Engine：两组构建、安装及各 4 项 C/C++ consumer 通过。
- Emscripten 5.0.6：关闭线程、Granit、Editor、Runtime、工具和完整测试集，静态核心构建、安装通过。
  CAORS 本身完整编译，CPU 材质/Mesh 实现没有关闭。编译使用 -fexceptions，
  消费端链接使用 -sDISABLE_EXCEPTION_CATCHING=0。
- Web 安装消费：Node 执行 C、application_world、C++、property_inspector 共 4 项通过。
  最终采用 --embed-file 和 Emscripten 虚拟文件系统，不依赖 NODERAWFS；
  覆盖公共接口、场景加载、Prefab 操作和反射属性检查。

日志位于 build/caors-core-unified-tests.log、build/caors-core-final-tests.log、
build/caors-core-sc-mat-s-consumer.log、build/caors-core-sc-mat-h-consumer.log、
build/web-caors-core-rebuild.log 和 build/web-caors-core-consumer-test.log。

## 边界

Web 验收为无渲染核心与安装消费，不是浏览器编辑器或网络 fetch 后端。
输入协作请求、浏览器异步 I/O 和同步/异步入口统一仍按
[收敛计划](../plans/PLAN-051-caors-convergence.md)继续推进；不宣称全工程迁移已完成。
