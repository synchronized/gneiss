<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-127：World 自有系统调度与宿主组合

状态：2026-11-20，实施中；P0/P1a 已完成，world_state 内部所有权与访问路径通过本地验证。
World system API、宿主迁移及多 World 调度验收尚未完成。
决策见 [ADR-130](../decisions/ADR-130-world-system-scheduling.md)，应用基础调度沿用
[PLAN-126](PLAN-126-application-scheduling.md)。纳入 0.52 本地架构整理，不包含远端发布授权。

## 当前事实与目标

当前 World 的具体实现为 world_state final : world，直接拥有 ECS、层级、场景实例和对象变化通知，
没有 add_system/update；内部实现位于 engine/src/function/scene/world_state.hpp/.cpp。
宿主通过 create_native() 获得 world_owner，在 Application 系统中捕获拥有者并执行逻辑。
world_render_binding 借用 World 与 Render，宿主先 flush_changes，再 synchronize。

目标：调用方通过 World 注册并执行该运行空间的系统，无需公开 world_session.world、
world_session.systems 或手动 systems.run(world, time)。允许游戏、预览使用不同系统列表，
同一系统实现复用到多个 World，系统运行状态分别拥有。

## 已确认职责

- Application 拥有共享服务、应用阶段和生命周期，仍不拥有或自动遍历 World 列表。
- World 拥有实体、组件及本 World 的系统列表；系统只处理显式传入的 World。
- 宿主拥有 world_owner，决定哪些 World 更新、是否暂停，以及更新先后。
- Render 同步仍在 Application synchronize 阶段执行，World::update 不隐式提交渲染。
- 文件、资产、窗口、UI 等应用级系统不得放进每个 World 重复推进。
- Document 不与 World 固定一对一。编辑会话选择投影目标；SceneInstance 继续表达 World 内
  归属与卸载，不自动拥有另一套系统列表。
- 不新增公开 world_schedule、World Manager 或系统基类；内部实现可复用适当的可调用项适配，
  不将整个 Application 生命周期调度器搬入 World。

## 已确认的内部组织

用唯一的 gneiss::world_state 替代 world::implementation，world_state final 公开继承 world，
直接拥有 ECS、场景槽位、变化记录及系统列表。world 是整个运行空间的接口门面，
不再保存 impl_ 或第二个 world_state 指针，也不再创建 world_impl 包装层。

这是受控的内部实现继承，不是用户扩展接口：world 构造/析构私有，
仅允许内部 world_state 构造基类；禁用复制和移动，移除当前 world 上的 final。
工厂只分配 world_state，返回既有 world_owner。world 不需要虚函数或虚析构；
world_deleter 在实现文件中转换为完整的 world_state 后删除，禁止通过 world* 直接 delete。
工厂、删除器与内部转换必须一起迁移，确保每个 world 引用确实指向 world_state 的基类部分。

| 类型 | 目标角色与成员 |
| --- | --- |
| world_state | 唯一具体实现和数据所有者；继承 world |
| world | 整个运行空间的接口，不独立存储另一份状态 |
| world_owner | 保留 unique_ptr 与自定义 deleter，唯一拥有具体对象 |
| scene_instance | 借用句柄：world* + scene_instance_id |
| game_object | 借用句柄：world* + entity_id |

后三类的外部所有权与句柄语义不变。GameObject、SceneInstance 不增加 impl/state、
引用计数或共享控制块；不能把三个入口都理解为可独立拥有数据的对象。

### 文件与命名空间

目标布局：

```text
engine/include/gneiss/engine/function/scene/
  world.hpp
  game_object.hpp
  scene_instance.hpp

engine/src/function/scene/
  world_state.hpp
  world_state.cpp
  world.cpp
  game_object.cpp
  scene_instance.cpp
```

world_state 位于 gneiss 命名空间，不增加 detail 命名空间或目录。
world.hpp 只前置声明并授予必要友元，完整定义仅在 src 内。
私有边界靠头文件位置和 target 的包含目录保证；不得为了访问状态将 src 包含路径
传播给其他模块。这个决定只针对 World 组织，不进行全项目 detail 清理。

world.cpp 实现运行空间入口，game_object.cpp 实现单对象入口，
scene_instance.cpp 实现场景归属组入口；world_state.cpp 集中维护共享的层级、
归属、销毁与变化记录逻辑。不按接口方法机械添加一套同名内部转发方法。

内部需要转换时采用声明于私有头、定义于 world.cpp 的 state_of(world&) / state_of(const world&) 小型函数，
所属线程断言留在实现文件，避免私有访问消费者额外引入断言库头；
用受工厂约束的静态转换获得实际对象，不暴露到 include 契约。
不是所有函数都必须经过新的访问类；现有 world_component_access 应按真实依赖审查，
迁移必要的模板/组件访问后删除纯转发部分，不为消灭名称暴露 EnTT。

add_system 模板只负责头文件中必要的 callable 适配，再进入非模板私有注册入口；
实际系统列表仍存放在 world_state 中。

### 对象行为与生命周期

- World 处理场景创建、系统注册与更新等整个运行空间的操作。
- SceneInstance 处理创建对象、遍历成员/根对象和卸载。
- GameObject 处理父子查询、重挂接、变换和通用组件操作，不绕行公开的
  world.entity_set_xxx/node_xxx 接口。
- 多入口共用同一份状态；重挂接、子树销毁与迁移必须统一维护归属、层级和变化通知。
- 对象/场景销毁后，仍存活 World 中的旧句柄通过既有身份校验失效，不改变 generation/domain 规则。
- World 销毁后，全部借用句柄不可再使用，包括 valid()；不新增能在 World 销毁后探活的控制块。
- world_render_binding 仍借用 World/Render，宿主先销毁绑定，再销毁 World，
  最后关闭共享服务；world_state 不反向拥有 binding 或服务。
- 系统 callable 与组件/场景成员的销毁顺序必须显式保证，避免迁移为直接成员后
  因 C++ 成员析构顺序改变既有资源释放行为。

## 目标接口

以下为目标接口草案，未实现；错误细节见后文实施规则。

```cpp
class world {
public:
  template<class F>
  void add_system(std::string name, F&& system);

  result<void> update(const frame_time& time);
};

// 可调用项的核心签名：
void update_animation(world& target, const frame_time& time);
result<void> update_game(world& target, const frame_time& time);
```

初期统一接受 (world&, const frame_time&) 参数，避免多种隐式注入规则。
按注册顺序串行执行；不根据类型名排序。首次 update 前完成注册，首次更新冻结列表。
系统名称用于诊断，在一个 World 内唯一；不同 World 可以重复使用同一名称。

create_native() 仍是当前工厂。此前讨论中的 create_world() 仅为示意，
本计划不顺带决定工厂重命名或改变现有 result<world_owner> 返回方式。

## 最小完整调用链

下面展示拟议 API 与现有 Application 生命周期的组合，失败沿已有结果返回：

```cpp
world_owner game;
app.on_start("game.create", [&]() -> result<void> {
  auto created = create_native();
  if (!created)
    return created.error();
  game = std::move(*created);
  game->add_system("animation", update_animation);
  game->add_system("gameplay", update_game);
  return {};
});

app.add_system(app::stage::update, "game.update",
    [&](const frame_time& time) -> result<void> {
      if (paused)
        return {};
      return game->update(time);
    });

app.on_stop("game.release", [&] {
  // 如安装了渲染绑定，应先释放绑定；共享服务随后由 Application 关闭。
  binding.reset();
  game.reset();
});
```

拥有者变量及借用捕获必须覆盖 Application 使用期。启动失败时 update 不执行，
on_stop 必须允许 World 尚未创建。应用系统捕获稳定的拥有者或会话，
不长期捕获可能在文档切换时被释放的 World 裸指针。

渲染链保持：本轮业务修改 → World.flush_changes → binding.synchronize → 帧提交。
暂停仅跳过对应 World 的模拟，编辑操作、共享服务和必要的渲染同步可以继续。

## 系统状态、失败与结构修改

以下是实施建议，编码前结合现有错误规范复核；偏离时更新本节，不默默更改契约：

- World 拥有注册 callable；有状态 callable 为每个 World 分别构造。外部引用仍是显式借用，
  API 不会自动隔离调用方主动共享的指针或引用。
- 支持 void/error/result<void> 返回，适配为统一执行结果；不允许任意返回类型被忽略。
- 可恢复失败终止本次 World 后续系统，返回原始错误并记录系统名称；不回滚已修改的对象。
  宿主决定停机或恢复，不自动重放整个更新。Application 调用链继续沿已有失败关闭协议。
- 所属线程、禁止重入、重复名称、冻结后注册属于编程契约，按现有断言规范处理，
  不为不可恢复误用增加一层 result。意外异常沿既有执行边界传播。
- 系统可以通过正常对象 API 修改组件/层级；不得跨结构修改保存无效借用。
- 不允许在系统执行中销毁当前 World、变更系统列表或递归调用 update。
  宿主在完整更新之外应用 World 替换/文档关闭请求；不为此新增引擎级通用切换队列。
- 带异步工作的系统应显式取消并排空后再销毁其 World，析构不隐式运行用户停止回调。
- 系统 callable 应在 ECS 数据销毁前释放；不允许析构重入 World 调度。
- 不引入第二套 World 生命周期、状态机或自动固定步长。初期时间参数由宿主提供；
  不同模拟时钟/固定步长以后按实际用例单独设计。

## 实施顺序

- [x] P0：复核目标签名、返回适配与生命周期规则，确认真实调用链；避免无职责公开调度对象。
- [x] P1a：将 implementation 迁移为 src/function/scene/world_state.hpp/.cpp 的内部派生实现，
  同步工厂、自定义删除器及三个接口的访问路径；删除旧 PImpl 存储与无职责转发，
  验证句柄身份和析构顺序不变。
- [ ] P1b：在 world_state 内实现系统存储、注册、顺序执行和最小诊断；保持 ECS 与服务边界。
  删除仅由 world_state_test 使用的旧 detail/system_scheduler，顺序和失败短路验收改走 World 入口。
- [ ] P2：选择一个现有宿主中确属 World 的更新逻辑迁移，服务完成分发与渲染提交保留应用层。
  不为填充系统列表新增虚构脚本、物理或 AI 模块。
- [ ] P3：验证两个 World 配置不同列表、独立有状态 callable、单独暂停/销毁及更新失败传播；
  不需要建立完整预览 UI 才能验证这一契约。
- [ ] P4：核对联合编辑用例：两个 SceneInstance 属于一个 World 时共用其调度，
  卸载一个实例不清空 World 系统或影响另一个实例。不实施完整多文档编辑器。
- [ ] P5：迁移真实调用方、更新 Reference/Concept，运行相关共享/静态构建和行为测试，
  删除迁移过程中产生的重复更新路径，并记录未覆盖范围。

## 当前验证

2026-11-20：P1a 完成共享/静态 Windows Clang Debug 相关构建。共享构建的对象、场景、
层级、内部状态、渲染绑定、组件适配及私有头可见性共 7 项测试通过；核心边界及负例、
World 服务边界共 3 项检查通过。静态构建的对象/场景生命周期和组件适配 3 项测试通过，
包含 World 销毁后的组件资源释放。git diff --check 通过。
本轮未进行 GPU 运行、其他平台或全量回归；系统调度与宿主更新迁移尚未实施。

## 验收与非目标

验收以单 World 游戏、编辑 World 加独立预览、同 World 多场景三种流程为边界。
至少证明注册顺序、空列表、首次冻结、独立状态、暂停隔离、失败短路及关闭顺序。
内部组织迁移复用现有对象/场景生命周期、跨 World 身份、层级和渲染绑定测试；
核对删除器调用完整派生析构、组件资源正常释放、句柄过期以及私有头不泄漏。
不通过 sizeof 或源码名称测试固化内部布局，不引入对已销毁 World 调用 valid() 的非法测试。
线程/重入/非法注册使用既有契约检查方式，不扩展大量重复的崩溃测试。
不得仅靠调度单元测试宣称宿主迁移完成；测试维护遵循 [测试指南](../guides/testing.md)。

非目标：依赖图、自动并行、自动参数注入、任意 Context、全局当前 World、
运行时随意增删系统、动态插件，以及为了预览复制 Asset Manager/Render Service。
不将其他引擎的渲染 Scene 直接当作本项目 World，不引入每文档必建 World 的限制。
