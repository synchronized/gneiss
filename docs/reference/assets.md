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

## 资产根目录

`gneiss_application_desc.asset_root` 与 `asset_root_length` 可在创建 Application 时挂载一个 UTF-8
目录。两者必须同时为空或同时有效；目录不存在时创建返回 `GNEISS_ERROR_NOT_FOUND`。旧版结构大小
仍可创建不挂载资产根的 Application。

目录 Provider 只读普通文件。读取时解析真实路径并确认其仍位于资产根内，因此路径穿越和指向根外
的符号链接不会被访问。当前尚未提供直接读取任意资产字节的公共接口；M-16 的类型 Loader 将使用
该内部 Provider。

## 缓存与生命周期

缓存以完整规范 URI 为键，同一 Application 内相同 URI 和资源类型复用同一实例。相同 URI 不能
同时解释为不同资源类型。调用方租约归零后，缓存项可被回收；Application 销毁会释放其全部缓存。
同步加载失败返回稳定结果码且不永久缓存，下一次获取会重试。

首版 API 仅限 Application 创建线程，尚不支持并发加载、异步 I/O 或热重载。
