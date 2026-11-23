<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 动态类型门面

## 变更

meta::object::dynamic_type 返回 meta::type，提供默认和显式注册表重载；对象只通过虚函数
提供静态 Ops，不新增注册表成员。未注册类型仍有效，无描述返回空句柄。
独立 dynamic_guid 删除，组件描述与编辑器使用类型句柄查询身份。
引用视图和序列化使用操作所属注册表解析动态类型，保留多继承完整对象地址。

object.hpp 保留对象基类，object_type.hpp 保存依赖 Ops 模板的 CRTP 辅助，避免循环包含。
构建源清单及内部接口头清单均已更新。

## 验证

编辑器及相关目标编译通过，5 项 CPU 测试全部通过：实例、分类 Ops、序列化、反射 JSON、
组件编辑。新增未注册动态类型、独立注册表解析、无描述空类型、多继承地址检查。
初次编译暴露循环头依赖与内部头清单遗漏，修复后完整重建相关目标。
日志：build/plan108-dynamic-type-consumer-build.log、build/plan108-dynamic-type-tests.log。
git diff --check 通过；未执行 GPU、大场景或远端操作。
可写/只读实例位置统一及 PLAN-108 其余阶段仍待完成。
