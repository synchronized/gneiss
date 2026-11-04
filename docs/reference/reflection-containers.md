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
读取元素不要求默认构造；vector 扩容、map 插入默认值和 optional 创建值才要求此能力。
自动成员绑定仍要求成员可移动赋值。vector<bool>、set、非字符串键及返回临时容器的 getter
不在内置支持范围。自定义序列/映射可使用下述 traits 适配；根 from 也可直接借用容器。

成员 `property_info.type.kind()` 提供结构类别；`scalar_kind()` 提供标量类别，容器为 invalid。
`object_type` 对借用成员记录实际 C++ 类型 ID。容器没有人为生成的稳定 GUID；
冻结时沿元素操作表检查对象叶子的 GUID 与运行期 ID，漏注册返回 not_found。
递归对象（如 vector<Node>）只检查叶子身份，不在冻结阶段展开无限对象树。

sequence_view 与 associative_view 的只读和可写版本都支持 size；顺序视图通过 at(index) 返回元素实例，越界返回 not_found。
可写顺序视图提供 resize。vector 保留原有前缀、新元素默认构造，缩小时销毁尾部。
array 的 is_resizable() 为 false，resize 仅接受原长度；JSON 长度必须恰好为 N，包含 N=0。
关联视图通过 keys() 获取排序后的自有键列表，通过 find(key) 获取值实例；键不可通过视图修改。
可写关联视图提供 insert、erase、clear，insert 已存在键时保留原值。optional 提供 has_value/get；
可写版本提供 ensure/reset，ensure 保留已有值，reset 销毁内部对象；optional 不提供 size/at。

四类视图分别定义在 meta/sequence_view.hpp、associative_view.hpp、optional_view.hpp、
reference_view.hpp；每类通过 basic_*_view<Mutable> 区分读写，不共用跨类别 container_view。
instance.hpp 汇集这些视图，原有容器调用方无需更换 include。引用视图语义见
[引用属性访问](reflection.md#引用属性访问)。

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

## 操作表与自定义适配

内部 `detail::type_ops` 保存类型身份、赋值操作及一个类别 variant；不再携带全部容器回调。
静态类别表分为 `scalar_ops`、`sequence_ops`、`associative_ops`、`optional_ops` 和 `reference_ops`，
位于 `src/engine/core/meta/ops/`，每个 C++ 类型共享静态表，不随对象分配。
optional 表使用 has_value/get/get_mut/reset/ensure，不借用 sequence 的 resize。
ref 表保留共享引用保活和赋值语义，与 optional 的内嵌值不同；目前只内置适配引擎 `ref<T>`，
不自动接管裸指针、shared_ptr 或 unique_ptr，也不新增动态操作注册中心。

在第一次生成该类型的反射操作表前专门化 `meta::sequence_traits<T>` 或
`meta::associative_traits<T>`；特化应放在所有使用该类型反射的翻译单元可见的同一头文件中。
类型不能同时声明为序列和映射。对象属性、名称与 GUID 仍由 type_builder 注册。

| traits | 必需成员 | 可选成员 |
| --- | --- | --- |
| sequence_traits | enabled=true、element、size(const T&)、at(const T&, size_t) | at_mut(T&, size_t)、resize(T&, size_t)、fixed_size=true |
| associative_traits | enabled=true、element、size(const T&)、keys(const T&)、find(const T&, string_view) | find_mut、insert、erase、clear |

size 返回 size_t；keys 返回自有 vector<string>，操作表统一按键排序。at/find 返回 const element*，
可写访问与 insert 返回 element*；find 未找到返回 nullptr。insert 保留已有值，erase 返回是否删除，
resize/clear 返回 void。回调返回真实、稳定的元素地址，不得返回临时对象或代理引用。
缺少可写回调时，对应可写视图操作返回 unsupported。固定长度序列改变长度返回 invalid_argument；
其他不支持 resize 的序列改变长度返回 unsupported。保持原长度允许作为无操作成功。

```cpp
namespace gneiss::engine::meta {
template<> struct sequence_traits<my_numbers> {
  static constexpr bool enabled = true;
  using element = float;
  static std::size_t size(const my_numbers& v) { return v.values.size(); }
  static const float* at(const my_numbers& v, std::size_t i) { return &v.values[i]; }
  static float* at_mut(my_numbers& v, std::size_t i) { return &v.values[i]; }
  static void resize(my_numbers& v, std::size_t n) { v.values.resize(n); }
};
}
```

适配器可抛出异常，instance/容器视图边界将 bad_alloc 映射为 out_of_memory，其他异常按现有接口
规则返回错误；不承诺回滚自定义回调产生的修改。只读适配可用于遍历和编码，不能因此推断其支持解码。
不可默认构造的 optional 已有值可访问和 ensure，清空后 ensure 返回 unsupported。
容器结构修改后的借用视图失效规则保持不变。
