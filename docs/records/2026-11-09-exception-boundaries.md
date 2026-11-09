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

## 属性、容器与序列化调用链追加验收

read_property/write_property、property::get、instance 的成员访问与赋值、各类容器视图，
以及 Serialization/JSON/value 入口移除重复异常转码，并同步调整 noexcept。
getter/setter、traits、codec 抛出的异常直接传播；非法值、缺少能力、越界等预期失败仍返回错误。
序列适配增加可选 max_size，resize 先检查上限；不再笼统捕获 length_error，避免吞掉元素构造异常。
纯句柄和不执行回调的能力查询保留 noexcept。

Windows Clang Debug：Editor 与相关目标编译通过，13 项定向 CPU 测试全部通过。
故障注入覆盖属性 getter/setter 的 bad_alloc/其他异常、codec 分配异常、序列适配器异常，
并验证容量越界返回 invalid_argument、失败时不交付属性快照。原容器与 JSON 正常往返测试继续通过。
一次提前执行旧测试导致 Windows 可执行文件占用，停止旧进程后重新构建、顺序执行全部 13 项通过；
该次失败不计为验收结果。

本批未运行 GPU、大场景和远端 CI。注册表创建/冻结/查询、核心 value 辅助操作、原生 JSON 分配失败
分类及外层资产/编辑器调用链仍需迁移；不能将本批 C++ 回调传播等同于全项目异常策略完成。

## 独立注册表与原生 JSON 分配追加验收

独立注册表创建、属性绑定及冻结删除异常转码和错误 noexcept；冻结仍使用临时索引，
预期的缺少类型、重复属性等错误保持原结果语义。故障注入验证能力查询异常传播、未发布冻结状态、
消除故障后可重试。默认表创建及查询入口尚未迁移。

JSON 适配器在 yyjson 的 C 调用返回后识别分配失败并抛出 bad_alloc；不从 C 分配回调抛异常。
read_into 使用带错误信息的解析入口，避免将分配失败误判为语法错误；输出使用 write_opts，
非法字符串等格式错误返回 invalid_argument。通用 reader/writer 返回错误的契约未改变。
自定义 yyjson 分配器故障注入通过；非法 JSON 输入和自定义 codec 输出非法 UTF-8 均返回预期错误。

Windows Clang Debug：Editor 与相关目标编译通过，8 项定向 CPU 测试全部通过；
clang-format 和 git diff --check 通过。未运行 GPU、大场景、远端 CI 或跨平台矩阵。
PLAN-109 仍未完成：默认表/查询、核心 value 辅助操作、资产/文件回调及上层执行链继续按计划审计。

## 默认表与查询调用链追加验收

默认注册表创建与注册表查询移除兜底异常转码，不再将锁/分配/类型能力异常伪装为 internal 或空句柄。
同步调整 type_of/find_type、property.type、element_type、dynamic_type、instance 创建、默认注册 builder、
序列化 context 与策略查询的 noexcept；纯句柄身份、名称与范围读取仍保持不抛异常。
正常未命中、未冻结、无效句柄继续遵循原有返回语义。

增加自定义元素能力故障注入，覆盖属性类型的 element_type 与注册表扫描查询，异常传播后正常查询仍可用；
编译期断言覆盖默认表/默认 builder 可抛异常和纯句柄读取不抛异常。
Windows Clang Debug：Editor 与相关目标编译通过，10 项定向 CPU 测试全部通过。
格式检查与 git diff --check 通过，未运行 GPU、大场景、远端 CI 或跨平台矩阵。

Meta/Serialization 目录已无原有 catch 兜底，但此结论不代表全项目计划完成：
核心 value 辅助操作、原生类型身份初始化、资产/文件服务、上层消费者与跨线程/C 回调仍需审计。

## 核心 value 操作追加验收

值校验、操作校验、编辑、重放与差异生成移除异常转码和错误 noexcept；预期输入错误继续返回错误值。
同步调整两个直接调用的 Prefab 覆盖接口。不可变值存储只在无数据时初始化空对象，
并允许空对象初始化的分配异常传播，避免已有值访问触发不必要的初始化。

独立测试进程注入五个操作的首个分配失败，验证异常传播、输入未修改与正常重试。
初次故障测试触发两个 MSVC 崩溃弹窗，已停止对应测试进程并显式安装致命处理器。
调试确认 MSVC Debug 容器代理还可能在标准库 noexcept 内分配；独立子进程验证该路径
非零退出并输出致命诊断，不依赖系统崩溃弹窗。运行库无法提供当前异常时允许通用致命诊断。
分配器替换仅限测试可执行文件，不改变引擎分配器或迭代器检查配置。

Windows Clang Debug：Editor 与相关目标编译通过，13 项定向 CPU 测试全部通过。
格式检查与 git diff --check 通过。未运行 GPU、大场景、远端 CI 或跨平台矩阵。
此批不覆盖全部分配位置；Prefab/编辑器等外层消费者及 PLAN-109 其余模块仍需继续迁移。

## Prefab 覆盖与组件描述追加验收

Prefab 覆盖校验、候选生成、重放及组件/变换覆盖编辑删除异常转码与错误 noexcept。
组件描述动态类型访问、身份复制、集合更新/替换及 modify_component 同步允许异常传播，
组件编辑类型摘要的分配异常不再返回普通错误。预期目标缺失、类型错误和领域约束仍走原有返回值。
来源描述与覆盖输入仍通过独立候选保护，不改变覆盖冲突的产品语义。

新增回调故障测试：组件集合更新抛出 bad_alloc、反射修改候选后抛出 runtime_error，
均验证异常到达调用方、原组件及集合不变；随后正常编辑成功。
Windows Clang Debug：Editor 和相关测试目标编译通过，5 项定向 CPU 测试通过
（Scene/Prefab 准备、Prefab 覆盖、组件描述、组件适配器）。格式检查及 git diff --check 通过。
未运行 GPU、大场景、远端 CI 或跨平台矩阵。

Scene 组合/准备与编辑器外层仍有旧转码；组件适配器其余运行时接口、资产/文件服务及
跨线程/C 回调仍待审计。不能将本批局部传播验证视为 PLAN-109 全部完成。
