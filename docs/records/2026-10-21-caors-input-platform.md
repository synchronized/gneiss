<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CAORS 输入平台统一验收

## 交付

删除输入功能开关与关闭分支；Engine 始终包含同一输入宿主。
CAORS Application 测试不再依赖直接 libuv 探针，可单独在无线程/Web 平台运行。
Runtime 线程池和协作模式均走输入请求、采用后初始化游戏模块，不再同步回退。

## 验证结果

- Windows Clang Debug：Application、VFS bridge、material-document 三项测试通过。
  Application 同时覆盖线程池和协作模式，含跨线程拒绝及隐藏窗口关闭。
- 原生静态 Engine，`GNEISS_TASK_THREADS=OFF`：Application 协作加载测试通过。
- Emscripten 5.0.6，静态 Engine，无 pthread：Application 测试通过；Node 24.19 执行
  嵌入 MEMFS 的输入资产，覆盖成功、失败保留旧映射、取消、退出及宿主执行器复用。
- 原生静态和共享 Engine：干净安装 C、C++、application_world、property_inspector
  消费测试分别 4/4 通过。
- Runtime `async-input-startup`：线程池/协作成功均记录 pending，输入应用先于游戏初始化；
  两种模式无效输入均拒绝初始化游戏；Windows 停止协议通过。

## 复现入口

原生与 Web 核心配置可关闭 `GNEISS_BUILD_TESTING`、工具和图形宿主，打开
`GNEISS_BUILD_CAORS_PROBE=ON` 与 `BUILD_TESTING=ON`。
无线程时构建 `gneiss_caors_application_test` 并运行 `ctest -R gneiss.caors.application`。
原生线程平台的额外 I/O/材质探针仍需 libuv 和 Granit 工具依赖。

Web 使用 Emscripten 工具链和 Ninja，开启 C++ 异常（本次 `-fexceptions`、
`-sDISABLE_EXCEPTION_CATCHING=0`），没有启用 NODERAWFS 或 pthread。
对应本地构建目录：`build/sc-mat-s`、`build/sc-mat-h`、`build/web-caors-core`、
`build/windows-clang-debug`。

## 限制与后续

同步 `load_action_map` 尚未合并；未注入缓存的底层资源通道仍待收敛。
协作 I/O 占用推进线程，不提供耗时上限。Web 验证仅为 Node/MEMFS，
不代表浏览器 fetch、图形窗口或完整 Web Editor 已验证。本轮未运行远端 Actions。
