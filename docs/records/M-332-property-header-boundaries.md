<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-332：属性与头文件边界收尾

2026-10-17，两项收尾已完成本地验证。范围仅为 DEV-051 的两项收尾，不代表 0.51 最终验收通过。

## 改动

- 公共反射值定义与 SDK 所有者拆分，旧公共包含入口、C ABI 和借用文本行为保持不变。
- Editor 属性面板、快照、旋转数学及撤销重做使用 C++ 强类型；内部 variant 拥有字符串。
  Runtime 在 IPC 边界转换协议值后调用同一原生 Registry 查询/写入入口。
- World 提供冻结的内置组件注册表工厂；宿主不再包含反射实现或 World 句柄表。
- 宿主、离线工具和任务消费者使用显式头清单，去除全 `src` 的传递可见性。
  拆出 scene_load_types 和 application_bridge；场景域、私有加载器及注册表不随接口传播。
- 补充文本快照生命周期、错误输出保持和头文件不可见检查；保留原有 C/SDK 与 IPC 回归。

## 验证

- Windows Clang Debug 共享/静态完整构建通过，包含 C11/C++20 独立公共头编译。
- 共享配置 26 项内部行为回归、11 项 C/SDK/属性协议回归通过；静态相同范围 37 项通过。
  覆盖原生文本快照在来源修改及 Registry 释放后的有效性、失败保持输出、跨线程 World 访问拒绝、
  属性写入、选择切换后的撤销重做、Prefab 编辑、Gizmo、场景加载及 IPC。
- 宿主与任务消费者的实际编译检查确认私有头不可见；依赖正反例通过。
  新增 Editor 父目录绕行检查后，单独重跑 2 项守卫测试通过。
- 共享/静态稳定 Runtime 安装验收通过；复制到 build 下的独立 C/C++ 消费者各 4 项通过。
  首轮安装发现新增值头漏入 FILE_SET，已补齐后重验；未通过源码路径补丁绕过安装缺失。
- 任务模块以 `GNEISS_TASK_THREADS=OFF` 脱离主工程独立配置/构建通过；不等同 Emscripten 实机验收。
- 对 6 个核心改动实现运行仓库 clang-tidy 规则，按修改行检查；处理发现的异常出口和格式问题后，
  受影响 3 个实现重查无修改行告警。修改行格式化、LF 与 `git diff --check` 检查通过。

构建与测试日志位于本地 `build/native-boundaries-*.log`。未运行 Linux 或新一轮完整 Sponza 性能对照。
原性能门槛失败和完整版本剩余验收继续以 [M-333](M-333-native-migration-verification.md) 为准。
