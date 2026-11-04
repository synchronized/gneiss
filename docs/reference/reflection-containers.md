<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 反射容器视图

`type_builder<T>::property<&T::member>` 自动识别以下直接成员，不需要单独注册容器类型：

| C++ 类型 | kind / 访问入口 | JSON |
| --- | --- | --- |
| `std::vector<T>`（排除 bool）和 `std::array<T, N>` | sequence / as_sequence() | 数组 |
| `std::map<std::string, T>`、`std::unordered_map<std::string, T>` | associative / as_associative() | 对象 |
| `std::optional<T>` | optional / as_optional() | 内容或 null |

元素可递归组合为标量、已注册对象或上述容器。直接嵌套 optional 因 null 歧义被编译期拒绝。
元素要求可默认构造，自动成员绑定要求成员可移动赋值。vector<bool>、set、非字符串键、
其他序列容器及返回临时容器的 getter 不在支持范围。根 from 仍要求注册的对象，容器通过成员/元素访问。

成员 `property_info.category` 提供结构类别；`kind` 保留标量类别，容器为 invalid。
`object_type` 对借用成员记录实际 C++ 类型 ID。容器没有人为生成的稳定 GUID；
冻结时沿元素操作表检查对象叶子的 GUID 与运行期 ID，漏注册返回 not_found。
递归对象（如 vector<Node>）只检查叶子身份，不在冻结阶段展开无限对象树。

只读和可写容器都支持 size；顺序视图通过 at(index) 返回元素实例，越界返回 not_found。
可写顺序视图提供 resize。vector 保留原有前缀、新元素默认构造，缩小时销毁尾部。
array 的 is_resizable() 为 false，resize 仅接受原长度；JSON 长度必须恰好为 N，包含 N=0。
关联视图通过 keys() 获取排序后的自有键列表，通过 find(key) 获取值实例；键不可通过视图修改。
可写关联视图提供 insert、erase、clear，insert 已存在键时保留原值。optional 提供 has_value/get；
可写版本提供 ensure/reset，ensure 保留已有值，reset 销毁内部对象。

```cpp
auto root = meta::instance::from(registry, object);
if (!root) return root.error();
auto items = root->property("items");
if (!items) return items.error();
auto sequence = items->as_sequence();
if (!sequence) return sequence.error();
auto element = sequence->at(0);
if (!element) return element.error();
auto snapshot = element->read_value(); // 仅当元素为标量时成功
```

容器和 optional 调用 read_value/write_value 返回 unsupported；不隐式复制整个容器。
标量元素沿用属性的有限数值、UTF-8 无 NUL 字符串和非零 GUID 校验。
结构修改、元素删除、optional reset 及父对象移动后，必须重新获取受影响的元素视图；
视图不检测悬空地址。视图保留注册表寿命，但不延长容器或元素寿命，调用方负责线程同步。

反射 JSON 缺失非必填容器字段时保留默认值并验证现有对象元素；显式字段替换整个容器内容。
未知对象字段沿用既有忽略规则；关联对象的每个键都是数据，重复键拒绝。
哈希容器按键排序编码，诊断包含索引或 JSON Pointer 转义键，如 `/groups/a~1b/0`。
容器和对象共用 64 层复合值深度限制，标量叶子不额外计层。
对象元素自定义 JSON codec 保持优先。解码失败释放临时根对象，不返回半成品，
但可写视图本身不提供事务回滚，用户回调的外部副作用也不会回滚。
