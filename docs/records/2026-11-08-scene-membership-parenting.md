<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景成员重新挂接边界验收

## 范围

推进 [PLAN-105](../plans/PLAN-105-game-object-runtime-model.md)。
World 在执行重新挂接前验证线程、句柄和场景成员归属。
不同场景的已登记成员不得互相挂接，检查目标祖先与整棵移动子树，
避免通过无场景登记的中间对象绕开校验。拒绝时不改变层级。
game_object 与迁移期 node_reparent 走同一入口。

复用现有成员清单，不新增第二套成员缓存；当前检查成本与成员数及层级深度有关。

## 验证

- Windows Clang Debug 默认构建通过。
- 84 项相关 CPU 回归全部通过。
- 新增两个同源场景、两种挂接入口、移动中间子树、目标中间父节点、
  拒绝后原层级不变及卸载另一场景不影响原对象的回归。
- 改动行格式整理及 git diff --check 通过。
- 未运行 GPU 大场景、远端 CI 或发布。

## 剩余工作

直接 World 对象销毁、动态成员创建与集合索引仍需统一；
Prefab 通用投影、运行来源记录删除和旧接口迁移未完成。
本记录不代表 PLAN-105 完成。
