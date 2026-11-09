<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-109 执行边界首批验收

日期：2026-11-09。关联 [PLAN-109](../plans/PLAN-109-exception-boundaries.md)。

## 审计与已实施范围

首批检索自有 src 约 970 处 catch，Meta/Serialization 约 132 处；数量为工作基线，不是完成指标。

| 调用链 | 审计结论及处理 |
| --- | --- |
| apps/editor、runtime、assetc、project | main 显式安装 terminate handler，顶层异常走 fatal；Editor/Runtime 原返回 99 的兜底同步修改 |
| task_scheduler | 执行器原将异常变为普通 failed，改为致命边界；工作线程外围也覆盖调度与收尾分配 |
| task_scheduler 构造、析构 | 构造启动失败须先停止已启动线程再重抛，保留此清理 catch；析构契约违例走 fatal |
| render_executor | 命令和帧原将任意异常变成 internal，改为整个工作线程入口捕获，覆盖队列完成通知 |
| Meta/Serialization | 重复错误转换与 noexcept 相互关联，后续成组迁移，当前未宣称完成 |
| 文件服务 | libuv/Fetch/IDBFS 回调必须阻止异常跨 C；适配、取消、迟到完成需后续逐项审计 |
| 其他宿主、工具和示例 | 局部文件系统适配、清理与错误转换仍待逐项分类；四个 main 接入不代表所有内部异常已传播 |

fatal 属于 Core，独立静态目标便于不链接 Engine 的调度器复用。不在库初始化时修改进程处理器。
紧急输出为固定文本，直接写标准错误后 `_Exit`，不使用普通日志队列、assert、atexit 或崩溃弹窗，
不承诺保存文档及运行析构。没有活动异常的 terminate 按契约违例诊断。

## 验证

- Windows Clang Debug：Editor、Runtime、Project、Assetc 与相关测试目标编译通过。
- 11 项定向测试通过：任务和渲染正常契约，以及直接终止、宿主异常、OOM、noexcept 违约、
  工作线程异常/OOM、协作调度、渲染帧和命令异常的子进程验证。
- 同一 fatal/调度器源码以 `-O2 -DNDEBUG -Wall -Wextra -Werror` 独立编译，7 项子进程探针通过。
- 正常失败夹具改为显式 task_outcome，原异常行为由独立致命子进程测试替代。

## 未验证与剩余事项

未运行 GPU、大场景、远端 CI、完整跨平台矩阵。
浏览器任务最小宿主已有 `-fexceptions` 编译/链接配置；新 fatal 目标同样启用该编译选项。
完整浏览器宿主的编译/链接选项和事件回调语义仍须独立验证，不由 Windows 通过推断。
仍需审计运行库/DLL 兼容性、其他执行入口及所有剩余 catch/noexcept；计划 1、2 暂不整体勾选完成。

## 注册调用链追加验收

metadata::insert、类型/枚举 builder、register_type/register_enum 不再逐层捕获异常转结果码。
同时移除内置数学/资源注册、initialize_meta 的错误 noexcept；初始化函数不再吞掉回调异常。
纯身份设置、句柄构造保留 noexcept。注册表仍先准备完整记录再提交，预期失败保持原错误语义。

新增测试覆盖用户扩展抛出 bad_alloc/其他异常时传播、RAII 清理、无半条记录；工厂显式返回错误仍在
commit 返回。默认表回调异常测试改为子进程致命验证，普通失败保留首次结果的测试继续通过。
Editor 及相关目标编译通过，17 项定向测试通过。
本批不代表 read_property、instance、容器访问、registry 创建/冻结、序列化及其他模块已完成迁移。
