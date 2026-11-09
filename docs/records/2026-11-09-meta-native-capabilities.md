<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 原生能力访问与属性句柄整理

## 实施范围

类型查询与内存视图不再要求所有叶类型已经注册；冻结前可查询静态能力，冻结后可借用未注册
对象、容器元素及 variant 分支。注册表的字段、枚举项等查询仍要求数据已经发布。
新增用例验证未注册对象不能被通用 JSON 当成空对象保存。

属性句柄及范围改为持有 detail::property_record 指针，删除 void 指针转换。属性支持 bool
查询有效性，仍保活注册表，不拥有或保活被访问对象；成员布局保持注册表引用加记录指针。

这是 PLAN-108 阶段 4a 的部分实施。type 的 descriptor/record 三成员布局、registered 查询、
直接返回句柄的查询 API 以及 instance 精简尚待完成，不代表阶段 4a–4c 已验收。

## 验证

原生能力改动的定向构建与 13 项 Meta/序列化 CPU 测试通过，日志为
build/plan108-native-capabilities-retest.log。旧测试直接对成功结果调用 error() 导致断言，
已改为检查新的成功语义；保留真实失败与未知对象持久化拒绝的断言。

属性句柄改动构建日志为 build/plan108-property-record-build.log，测试日志为
build/plan108-property-record-tests.log。未执行 GPU、大场景或远端操作。
