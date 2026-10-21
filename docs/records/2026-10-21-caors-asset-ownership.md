<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CAORS 输入资源管理所有权迁移

## 交付

删除 function/input/caors_input_host 的头文件与实现。CAORS manager、输入类型缓存、来源绑定、
GUID 分配、工作 scope 和完成队列直接由 engine::asset_manager 的私有 resource_state 拥有。
不新建另一个通用宿主包装，不增加运行时开关，也不改变公共 ABI。

输入 action_map_application 改为接收资产层创建的 load_request，不再访问 manager；
load/reload 策略由资产层决定。输入格式 Loader、映射验证和 InputService 保持各自职责。
采用时机由 Application 保持，取消与退出仍遵循原 scope 排空顺序。

## 验证

Windows Clang Debug 通过 6 项：caors.application、caors.asset-probe、caors.vfs-bridge、
runtime.async-input-startup、c_application、cpp_input。
覆盖输入加载、失败保留旧映射、请求替换/取消、在途关闭、复用执行器、Runtime 启动与停止。
原生无线程静态 Engine 和 Emscripten Node/MEMFS 的 Application 测试分别通过。
源码检查确认 function/input 不再声明 caors::assets::asset_manager 或 cache 成员。

## 剩余范围

resource_state 当前注册输入资源；材质/Mesh CPU 文档缓存仍沿用既有实现，
不能据此宣称所有资源已统一到一个 CAORS database。同步缓存发布与旧异步请求代次协调
仍待完成。本次未运行远端 Actions，未验证浏览器网络 I/O。
