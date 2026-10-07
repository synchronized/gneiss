<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 资产 URI、目录挂载与缓存

## URI 规则

Gneiss 当前只支持规范形式的 `asset://` URI，例如：

```text
asset://models/triangle.mesh
asset://纹理/石头.png
```

URI 使用 UTF-8 和正斜杠，scheme 区分大小写。空路径、空路径段、`.`、`..`、控制字符、反斜杠、
冒号、查询、片段和百分号编码均返回 `GNEISS_ERROR_INVALID_ARGUMENT`。校验函数不访问文件系统：

```c
gneiss_result result = gneiss_asset_uri_validate(uri, uri_length);
```

## VFS 与资产根目录

`gneiss_application_desc.asset_root` 与 `asset_root_length` 可在创建 Application 时挂载一个 UTF-8
目录。两者必须同时为空或同时有效；目录不存在时创建返回 `GNEISS_ERROR_NOT_FOUND`。旧版结构大小
仍可创建不挂载资产根的 Application。

Application 内部将资产根作为 `asset://` 根挂载到虚拟文件系统（VFS）。VFS 按最长挂载点前缀
路由，文件系统后端只接收相对路径，因此未来可以把子目录挂载到归档或网络后端而不修改 Loader。

当前唯一实现是只读 `native_file_system`。它读取普通文件，并解析真实路径确认目标仍位于资产根内，
因此路径穿越和指向根外的符号链接不会被访问。VFS 后端接口目前保持内部；尚未实现目录枚举、写入、
归档或网络后端。Windows 原生范围来源另提供可轮询 I/O；类型 Loader 通过该 VFS 读取文件。

## 缓存与生命周期

缓存以完整规范 URI 为键，同一 Application 内相同 URI 和资源类型复用同一实例。相同 URI 不能
同时解释为不同资源类型。调用方租约归零后，缓存项可被回收；Application 销毁会释放其全部缓存。
同步加载失败返回稳定结果码且不永久缓存，下一次获取会重试。

公共同步 API 仍限 Application 创建线程；宿主内部的异步场景与资产服务使用注入的任务执行器。

## 内部异步准备

Asset 层拥有准备状态、输入缓冲、候选和来源身份。每个执行步骤共用读取字节预算，
并限制状态转换次数；短操作可在同一步连续完成，避免每个转换都等待下一宿主帧。
时间边界在操作之间检查，不能抢占单次 JSON/PNG 解析或同步文件系统调用。
thread 与 cooperative 使用同一实现；cooperative 在步骤之间返回宿主，没有隐式后台线程。

来源包装的分步读取入口在完整摘要计算后登记身份，嵌套包装复用这一扫描。
最终复验逐段比较全部来源；修改、短读、取消或校验错误都不得发布未验证的候选。
只提供整文件读取的内部自定义后端保留同步回退，不承诺同样的单步时间上界。
Windows 原生后端提供 OVERLAPPED 范围请求，跨步骤保留自有缓冲；NOT_READY 时返回宿主，
请求销毁前完成取消回收。同步 API 不采用轮询忙等。Linux 原生范围读取及未实现这一可选契约的
后端仍同步执行，不保证文件系统等待上界；也不保证外部文件在验证后永远不变。

## 场景请求的清理快照

Runtime 的 `scene` 域消息可携带内部 `budget` 对象。容量字段为无符号 64 位字节数：
`candidate_logical_bytes`、`candidate_cpu_data_bytes`、`application_logical_bytes`、
`application_cpu_data_bytes`、`available_bytes`、`upload_reserved_bytes`、`peak_upload_bytes`。
候选为已完成批次统计，Application 为资源账本，上传预留为当前子批次估算；均不是进程 RSS 或实际显存。

可选布尔字段 `cleanup_complete` 只有在失败/取消请求的内部状态和上传租约销毁后才为 true。
此时候选分项保留回收前的诊断值，Application 和上传预留重新采样回收后的账本。
活动场景、共享缓存和旧渲染帧仍可合法持有资源；该字段不表示整个 Application 已清空。
成功激活将候选转移给活动场景，不使用这个标记表示释放。
旧消息省略该字段按 false（未确认）处理；错误类型被拒绝，既有 IPC 域版本不变。
