<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-296：Prefab 刷新令牌的 C++ 所有权

日期：2026-10-06。

## 缺口与改动

原 C++ 场景接口仅返回裸刷新令牌，调用方须自行保存所属场景并释放历史租约。
新增移动专属 `scene_prefab_refresh`，只保存 Application、场景和令牌，不复制事务状态或延长父对象寿命。
提供 toggle、reset、release；旧裸令牌重载保留，C ABI 与引擎业务实现不变。

新的刷新重载要求输出拥有者为空，在修改投影之前拒绝非空输出，避免隐式丢弃原有历史。
释放历史不撤销当前投影。具体线程、错误、父对象失效及转移规则以
[Scene Reference](../reference/scene-instance.md#节点枚举) 为准。

## 验证

- Windows Clang Debug 共享与静态配置全量构建通过，包含公共头独立编译。
- 包装、生命周期、原 Prefab 行为、清单及安装稳定运行时专项均 17/17 通过：共享 8.04 秒、静态 21.09 秒。
- 新增测试覆盖空拥有者、失败刷新、非空输出拒绝、移动构造/覆盖、release 后手动释放、作用域析构、
  外部释放、场景移动/卸载、Application 先失效，以及跨线程显式失败与所属线程重试。
- 子进程验证跨线程析构按契约终止，不产生测试窗口。
- 安装前缀的独立 C/C++/反射消费者两种链接方式均 3/3 通过，C++ 消费者实际执行新 RAII 刷新和 toggle。
- clang-tidy 检查新测试；既有 Application 初始化宏仍有有符号位运算告警，不降低检查等级。
- 首轮测试误将空令牌 toggle 预期为 invalid_handle；根据 C 契约修正为 invalid_argument，未修改底层错误语义。

接口清单完成这 3 个函数的语义映射，累计 39/102 reviewed；完整 SDK 审计仍未结束。
本轮未运行远端矩阵及完整静态 CTest，不作为版本发布验收。
