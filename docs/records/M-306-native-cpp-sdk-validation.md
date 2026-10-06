<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-306：0.47.0 原生 C++ SDK 验收

2026-10-06，基于 0.46.0，原生 SDK 实现及整版本地/远端验收已完成，待合并与源码发布。
范围以 [VER-047](../plans/VER-047-0.47.0-native-cpp-sdk.md) 为准。

## 实施结果

- Application 配置、帧时间、诊断与日志回调采用原生类型，稳定回调表随拥有者及 released_application 转移。
- World/Scene 使用原生 Transform、Camera、MeshRenderer、强类型 ID、标志和文本视图；查询失败保留输出，UUID 映射逐元素转换。
- Render 描述与 UI/Debug 提交使用 span/array、原生枚举及强类型资源 ID；转换存储只借用至调用返回，分配错误通过 result 返回。
- Reflection 属性采用 variant，元数据查询拥有字段数组、借用名称；访问器桥接存储随 Registry 移动，release 返回 released_type_registry。
- Game Module 使用编译期回调表生成桥接，业务回调使用原生参数；C 查询只写已知字段，保留扩展尾部及失败输出。游戏模板已迁移。
- Core 原生结果、实体/RID、版本和 World 映射完成审计，常规 CTest 已启用严格原生清单。
- 26 个旧路径转发头删除，干净安装含 28 个规范公共头；路径删除专项见 [M-305](M-305-remove-forwarding-headers.md)。

详细所有权和迁移规则集中在 [C++ SDK 参考](../reference/cpp-sdk.md)，不在本记录复制接口定义。
Input 与日志先行验证分别见 [M-301/303](M-301-303-native-input-sdk.md)、[M-304](M-304-native-log-message.md)。

## 专项验证

Windows Clang 23.1.1 Debug，警告视为错误。Application 生命周期、Transform/Camera、Render 提交、
Scene/Prefab、Reflection 元数据及属性访问均完成共享/静态专项。相关阶段的全量构建与安装消费者通过。

- 新增无窗口原生 UI/Debug 提交测试，覆盖正常提交、非法索引/线宽、空列表及错误提交阶段。
- 新增原生属性访问测试，覆盖九类值、未知/非法表示、描述离开作用域、绑定冲突、冻结、移动、释放/接管及销毁失效。
- Game Module 共享专项 8/8、静态专项 7/7；安装游戏模板创建、编译、Runtime 启动和目录包流程通过。
- Property Inspector 示例改用普通原生 C++ 接口，共享/静态安装消费者各 3/3 通过。
- 改动源码格式与 diff 检查通过，Game Module 和原生属性访问的定向 clang-tidy 已复核；不声称全仓历史告警清零。

日志位于本地 build/0.47-*。这些是分组验证，不代替下述最终版本矩阵。

## 整版验收

102 个公共 C 导出、61 个同版本私有 C++ 导出与 0.46 逐项一致。规范 C 头仅位移字面量增加无符号
后缀，函数声明与结构布局不变。清单覆盖 102 个函数、96 个类型、281 个常量，严格模式无 pending。
类型中 2 项、常量中 31 项是有理由的 ABI 基础设施例外。

共享首轮完整 CTest 为 188/189；灯廊 Runtime 工作流在第二次启动等待场景激活的 5 秒期限内失败。
日志显示新进程已创建 Application，未记录崩溃；当时并行进行静态全量构建，负载关联尚待确认。
保持原测试阈值，在构建结束后独立连续复验 3 次均通过（24.41 秒合计）。
这是负载相关的时间敏感迹象，不把重跑通过称为根因修复；首轮失败记录保留。

静态完整 CTest 186/186 通过（139.26 秒）。两种全新 SDK 安装前缀各含 28 个公共头，
26 个旧路径负向配置检查通过；共享/静态消费者各 3/3 通过。
共享最终完整回归 189/189 通过（207.51 秒），包含原失败的灯廊工作流。
最终代码同时补充了 Application/Log 描述指定初始化的显式默认值；共享/静态独立 C11/C++20 头构建通过。
候选 `375cb2a` 已触发首轮 Windows/Linux 完整矩阵。Linux GCC/Clang 核心任务及共享图形构建
发现 reflection.hpp 使用 std::exchange 却没有显式包含 utility；Windows 传递包含掩盖了问题。
已补显式标准头，不改接口及运行逻辑。本地独立头重编译与 Game Module/Reflection/属性访问 3/3 通过；
修复候选继续验证远端矩阵，不跳过失败编译单元。

## 已知限制

这是有意的 C++ 源码不兼容版本，消费者须迁移并重新编译；C++ 二进制 ABI 不作稳定承诺。
数组转换有临时分配成本，借用文本与用户数据仍有明确寿命要求；原生 noexcept 回调不得抛异常。
不升级 Granit、不开发插件或公开 Editor API，不新增加载性能承诺，也未重新测量第三方大场景。
0.46 曾观察到 Linux IPC SIGPIPE，根因未定位；本版尚未将其描述为已修复，完整矩阵继续覆盖该用例。

### 工具链与门禁兼容性修复

utility 修复候选 f1b5f24 的 Linux 业务回归已执行，剩余失败来自 CMake 负向检查：
独立执行类型清单脚本没有初始化 CMP0057 策略；CMake 3.25 的纯 C try_compile 无法识别
导入 Engine 目标的 cxx_std_20 特性。前者补同一最低版本声明，后者增加不包含被测头的 C++
语言单元；被测 C 头仍单独按 C11 编译，仍须以“缺少头文件”失败。

MSVC 首轮在 Game Module 夹具发现全局 state 遮蔽告警，改用明确的 test_state；
本机 /W4 /WX 进一步发现空回调表会实例化未使用的空指针调用桥接，改用 if constexpr
仅生成存在的回调。没有关闭警告或删除缺失回调用例。

本机 MSVC C++20 /W4 /WX 编译通过；Clang 独立头、模块测试及定向静态检查通过。
额外在构建目录运行 CMake 3.25.2：清单正反例通过，共享/静态安装消费者各 3/3 通过，
26 个旧路径探针均按预期失败；临时恢复旧头的反例被明确拒绝，随后移除该临时夹具。
不把本机旧 MinGW 8.1 当作 C++20 GCC 验收工具，GCC 结果以远端矩阵为准。

为避免已知失败候选继续消耗验证资源，取消了尚未完成的 Windows 首轮及 f1b5f24 运行；
后续在统一修复候选上重新验证 Windows/Linux，结果见下文。

### 统一修复候选结果

候选 `84a3e98` 的本地 Windows Clang Debug 完整共享回归 189/189（209.94 秒）、
静态回归 186/186（109.81 秒）通过；两种最终安装 SDK 消费者再次各 3/3 通过。
先完成构建再串行执行图形回归，没有调整原测试时限。

[Linux 37483029143](https://github.com/synchronized/gneiss/actions/runs/37483029143) 8/8 通过：
Clang/GCC 的四组核心各 101/101，图形共享 187/187、静态 184/184，Sanitizer 与浏览器调度通过。
两组图形任务的 runtime.ipc-session 均通过，这不构成既有 SIGPIPE 根因修复证据。
[Windows 37483022332](https://github.com/synchronized/gneiss/actions/runs/37483022332) 4/4 通过，
包含共享/静态安装消费者及两组 MSVC 宿主。最终同一候选合计 12/12 通过。
原始候选身份、任务状态及失败/取消轮次见[远端检查摘要](artifacts/0.47-release-checks.json)。

合并前仅补验收、发布说明及一条 Reflection 借用注释，不改变代码语义、构建或测试；不重复触发矩阵。
发布不附预编译程序或第三方大场景资产。
