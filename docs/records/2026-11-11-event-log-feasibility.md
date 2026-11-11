<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 事件与日志适配核查

日期：2026-11-11。范围：源码审查与独立 CPU 探针，不是生产迁移验收。

## EnTT

读取仓库 3rd/entt 的 3.15.0 sigh.hpp：publish 遍历内部 delegate 容器，连接保存信号地址，
不拥有监听者，也不提供所需的发布中任意修改或源销毁保护。
在 build/event-design-probe.cpp 编译运行独立探针，命令：

```powershell
clang++ -std=c++20 -I3rd/entt/src build/event-design-probe.cpp -o build/event-design-probe.exe
./build/event-design-probe.exe
```

结果 exit 0：成员函数接收、scoped_connection 析构断开、借用已命名 lambda 并显式断开、
回调异常向调用方传播均通过。探针仅放构建目录，不新增永久测试目标。
未执行原生悬空连接、发布中销毁或跨线程数据竞争测试；包装层能力尚未实现。

## 日志

读取用户提供的 Ludens Log.h/Log.cpp 以及 Gneiss core/log、runtime_log.cpp、runtime_log_protocol.cpp。
Ludens 的轻量频道调用值得借鉴，但频道表无同步、仅 32 位哈希身份、持锁 observer 回调不能直接移植。
当前 Gneiss 已有唯一有界异步队列，迁移应复用其能力，不创建第二套分发。

核对 [spdlog v1.15.3 log_msg](https://github.com/gabime/spdlog/blob/v1.15.3/include/spdlog/details/log_msg.h)
及 [logger 实现](https://github.com/gabime/spdlog/blob/v1.15.3/include/spdlog/logger-inl.h)：标准记录没有
Gneiss context/result/sequence 字段，标准 logger 路径会处理 Sink 异常，需要审查与 PLAN-109 的关系。
决定将 spdlog 用于同步输出适配，原生记录直接交给 Console/IPC，不把元数据编码进日志文本再解析。

未下载或引入 spdlog，未编译其适配；浏览器、文件失败与 OOM 边界尚待验证。
本次文档与 EnTT 探针通过不等于两套新系统已经可用。实施清单见 [PLAN-116](../plans/PLAN-116-events-and-logging.md)。
