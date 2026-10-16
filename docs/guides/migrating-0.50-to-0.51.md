<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 从 0.50 迁移到 0.51 开发版

本指南描述当前开发分支的源码变化，不表示 0.51 已发布。

## 外部 C++ SDK

头文件路径不变。拥有或借用运行时对象的包装移入 `gneiss::sdk`，不提供旧顶层别名。
C ABI 函数、结构布局和动态模块协议保持不变。类型名继续使用小写下划线。

| 原名称 | 当前名称 |
| --- | --- |
| gneiss::application / application_ref / released_application | gneiss::sdk 下的同名类型 |
| gneiss::world / world_ref | gneiss::sdk 下的同名类型 |
| gneiss::scene_instance / scene_prefab_refresh | gneiss::sdk 下的同名类型 |
| gneiss::mesh / material / texture | gneiss::sdk 下的同名类型 |
| gneiss::type_registry / released_type_registry | gneiss::sdk 下的同名类型 |
| gneiss::game_context / game_module 及游戏模块描述、回调、更新时间 | gneiss::sdk 下的同名类型 |

Application 配置及回调类型也在 `gneiss::sdk`。通用结果、实体/节点/资源标识、Transform、
渲染描述和反射属性值仍在 `gneiss`；它们是值类型，不拥有另一套运行时。不要仅凭命名空间
判断所有权，应查看具体类型的生命周期说明。

```cpp
#include <gneiss/engine/application.hpp>

int main() {
  gneiss::sdk::application app;
  if (gneiss::sdk::application::create({}, app).failed()) return 1;
  gneiss::sdk::world_ref world;
  if (app.get_world(world).failed()) return 2;
  gneiss::entity_id entity;
  return world.create_entity(entity).ok() ? 0 : 3;
}
```

`world_ref` 不延长 Application 寿命。场景、资源和历史令牌须先于父对象释放；错误线程析构
仍按原契约终止进程。需要处理关闭错误时显式调用 `reset()`。

## 同源码宿主

Editor/Runtime 使用内部原生对象与服务接口。外部消费者继续使用安装 SDK，不能包含 `src/`
私有头。原生 Application 拥有运行服务和场景域，场景域拥有 World 与场景实例；Scene/Prefab
只借用所属 World，共享资源由原有资产服务管理。

Editor 会话、资产事务及历史命令须在宿主 Application 之前关闭。场景切换后不可继续借用旧域
服务；会话通过活动域身份校验，候选取消和回收继续由既有事务流程推进。
动态游戏模块和反射回调的真实 ABI 边界保留，不应按普通内部调用机械删除。

架构决策见 [ADR-062](../decisions/ADR-062-internal-cpp-and-sdk-boundaries.md)，构建与安装验证见
[构建指南](building.md)。当前迁移进度以 [DEV-051](../plans/DEV-051-architecture-boundaries.md) 为准。
