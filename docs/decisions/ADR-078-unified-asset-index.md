<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-078：资源管理器唯一持有资产索引

- 状态：已接受。
- 日期：2026-10-24。

## 决策

asset_manager 持有唯一 resource_cache。Prefab 与 Render Loader 借用该缓存，
Application 不再拥有单独渲染缓存；通用类型请求不再另存 current 资源。
缓存键统一为来源域、URI、std::type_index；类型标识只用于本进程，不序列化或进入公共 ABI。
缓存有效负载采用 shared_ptr<const void> 类型擦除；渲染槽位内部的请求顺序和依赖引用
由所属线程显式更新，CPU 描述保持只读，不使用 const_cast。

在途请求表只保存请求顺序与观察状态，任务完成后写入唯一缓存。
CPU 输出使用共享控制块的别名引用保活缓存条目，旧引用、未领取结果与在途任务不会被回收。
迟到结果仍交给其观察者，但不能覆盖更新请求的缓存。

## 边界与取舍

C++ 类型代替缓存内部数字标签，避免通用类型与渲染类型另建两套索引。
VFS 的资产格式分类仍可使用原数字标签；解析来源域不需要伪造资产类型。
同 URI 的不同类型或来源可共存，替代 ADR-077 中旧 URI 独占条目的限制。
CPU 描述重载与渲染稳定 RID 更新保持原规则；缓存统一不等于加载调度统一。

唯一缓存延长到管理器销毁，但依然可以显式回收无人引用的条目。
管理器与所有渲染资产引用须先于 Render Service 销毁，不增加跨线程销毁队列。
不把 Render 依赖移入 asset_manager；功能层继续装配 Loader。
