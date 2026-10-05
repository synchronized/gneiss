<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-296：既有 C++ 拥有者的关闭与转移

日期：2026-10-05。完成 Application、World、Scene、Type Registry 的所有权子项；不代表完整 SDK 或版本验收完成。

## 缺陷与修正

旧包装在 C 销毁返回错误时仍清空句柄，跨线程关闭会遗失仍存活资源的所有权；工厂替换现有输出
也可能覆盖无法销毁的旧对象。修复后显式关闭保留失败结果与句柄，工厂使用临时候选保证失败回收，
析构和移动覆盖采用已有 Render 包装的失败终止策略。所有权转移补为公开 release，Scene 同时提供
非拥有型 owner 查询。没有新增共享状态、引用计数或后台销毁队列，C 头与导出保持不变。

当前契约以 [Application](../reference/application.md#生命周期)、[World](../reference/world.md#c-包装)、
[Scene](../reference/scene-instance.md#所有权与卸载) 和 [Reflection](../reference/reflection.md#当前范围)
为准；返回类型的源码兼容边界已写入 Reference 与 ADR。

## 验证

新增生命周期测试覆盖：零值幂等关闭、移动构造与覆盖、成功替换、失败创建、失败替换回收候选、
release 后手动销毁、外部销毁后清空、跨线程关闭失败后所属线程重试、父 Application 先失效。
Application 使用 shutdown 计数验证候选回收；Registry 验证外部同步后跨线程关闭允许成功。

另用子进程验证 World、Application、Scene 跨线程析构触发终止处理器。Windows 首轮因处理器
安装在主线程而超时；改在实际析构线程安装后通过，不依赖 CRT 跨线程传播处理器。不产生测试窗口。

共享库、静态库全量构建通过，包含公共头独立编译。最终专项两种链接方式均 12/12 通过
（共享库 7.78 秒，静态库 17.77 秒），覆盖上述新测试、已有 Render 拥有者、World/Scene/
Application/Reflection 包装、旧 ABI、安装消费者与审计清单检查。

静态检查新增测试仅保留既有 Application 初始化宏的有符号位运算告警，不关闭检查等级。
未运行远端 Actions 或 Vulkan validation；该组测试不代替真实 GPU 与外部大场景验收。

## 剩余项

接口审计新增 8 项，累计 36/102 reviewed，66 项仍 pending。类型、回调及其他包装可用性继续审查；
本次未将场景 load 的完整 I/O 成败路径标为已审查，也不声称内部语义配置或后端目录已完成迁移。
