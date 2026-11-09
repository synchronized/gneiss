<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 类型句柄布局精简

## 变更

meta::type 收敛为 registry_ref、原生 type_ops 指针和可选 type_record 指针。
删除句柄中重复保存的 type_id、GUID、类别、标量类别和枚举标记。
类型支持 bool 和 registered 两种独立判断；没有原生 Ops 的运行时注册类型仍可查询记录身份，
不会凭空获得 C++ 内存操作能力。枚举显示项也由冻结注册记录提供。

名称、版本、扩展、属性列表及属性查找直接访问句柄保活的冻结记录，删除对应的重复注册表查询。
冻结前取得的类型句柄保持未注册状态；需要新发布的记录时重新查询。

本批尚未迁移 type_of/find_type 等函数的 result 返回形式；instance 暂时通过原有身份解析 type，
其成员精简与 dynamic_type 迁移仍待阶段 4b 实施。PLAN-108 阶段 4a–4c 不因此标记完成。

## 验证

新增布局静态断言，以及无效句柄、未注册能力、冻结前后句柄状态、运行时注册类型测试。
定向测试及重新构建后的 13 项 Meta/序列化 CPU 测试通过。
边界测试原先将字符串属性的 GUID 写成所属对象 GUID，现改为静态字符串类型的真实身份。
构建日志为 build/plan108-type-handle-regression-build.log，测试日志为
build/plan108-type-handle-regression-tests.log。git diff --check 通过。
未执行 GPU、大场景或远端操作。
