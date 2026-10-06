<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-294：Application 语义配置与平台窗口归属

日期：2026-10-06。

## 变更

Application 注册表和状态改为接收内部 `application_configuration`，不再存储版本化 C 描述。
C 入口负责旧布局补齐、保留字段与指针/长度校验，将路径转换为初始化期借用视图，将窗口开关转换为 bool。
配置不持有另一套应用状态；生命周期、时钟、诊断、日志回调保留实际 C ABI 签名及原上下文寿命。

Platform 只消费 `window_configuration`，不接收 Application、资产、环境或用户回调配置。
窗口实现、原生窗口视图与后端枚举归 `gneiss::platform`；Render 仅借用 Platform 视图，取消名称上
对 Application 的归属。接口仍不安装，不改变公共 C ABI 或 Granit。

保留空标题默认值、窗口尺寸/标志映射、资产目录挂载错误及初始化回滚。增加资产目录指针/长度不匹配
的 C API 回归，失败清空输出句柄。边界检查增加 Application/Platform 消费 C 描述的反例与语义配置正例。

## 私有导出核对

[私有接口清单](artifacts/0.45-private-interface-inventory.json)登记 5 个头、61 个 C++ 导出声明：

- World 27 项与 Reflection 13 项供内部调用及同一 DLL 的原生/C 入口一致性测试；宿主常规操作走公开 SDK。
- Game Context 3 项由 Runtime 管理模块上下文和可信日志来源，模块本身不获得管理权。
- 完整场景加载 6 项与资产/采样 12 项为同版本宿主接口，保持服务、请求和执行器的唯一所有权。

这些符号当前仍由共享库导出，头文件不安装，不是第二套稳定 SDK；不能根据 DLL 导出表推断为插件 API。
没有把加载服务或调度器为追求包装数量自动升为公共接口。更一般的异步 API 仍需单独设计。

## 验证

- 窗口归属子阶段共享构建与窗口/渲染/帧包/像素/边界专项 9/9 通过，23.38 秒。
- Application 配置转换后共享完整构建及 CTest 181/181 通过，189.79 秒。
- 静态完整构建与 CTest 178/178 通过，115.09 秒；共享/静态安装后的 C、C++、属性检查器 consumer 均 3/3 通过。
- 共享 DLL 导出表实测 163 项：102 个公共 C 符号与基线一致，另 61 个为上述私有 C++ 导出。
- clang-tidy 检查新适配路径；保留既有 C 标志宏引发的 signed-bitwise 和 poll 相邻 bool 引用告警，未降低等级。
- 没有运行远端矩阵；Render/Scene 的其他版本化描述转换仍待收口，本记录不代表整版完成。
