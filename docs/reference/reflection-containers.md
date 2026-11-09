<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 反射容器视图

`type_builder<T>::property<&T::member>` 自动识别以下直接成员，不需要单独注册容器类型：

| C++ 类型 | kind / 访问入口 | JSON |
| --- | --- | --- |
| `std::vector<T>`（排除 bool）和 `std::array<T, N>` | sequence / as_sequence() | 数组 |
| `std::map<std::string, T>`、`std::unordered_map<std::string, T>` | associative / as_associative() | 对象 |

元素可递归组合为标量、已注册对象或上述容器。std::optional<T> 的规则见[可选值反射](reflection.md#可选值反射)。
读取元素不要求默认构造；vector 扩容、map 插入默认值才要求此能力。
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
可写关联视图提供 insert、erase、clear，insert 已存在键时保留原值。

序列、关联与引用视图分别定义在 meta/sequence_view.hpp、associative_view.hpp、
reference_view.hpp；每类通过 basic_*_view<Mutable> 区分读写，不共用跨类别 container_view。
instance.hpp 汇集这些视图，原有容器调用方无需更换 include。引用视图语义见
[引用属性访问](reflection.md#引用属性访问)。

```cpp
auto root = meta::view(registry, std::as_const(object));
if (!root) return root.error();
auto items = root->member("items");
if (!items) return items.error();
auto sequence = items->as_sequence();
if (!sequence) return sequence.error();
auto element = sequence->at(0);
if (!element) return element.error();
auto snapshot = element->read_value(); // 仅当元素为标量时成功
```

容器调用 read_value 或标量 assign 返回 unsupported；不隐式复制整个容器。
标量元素沿用属性的有限数值、UTF-8 无 NUL 字符串校验；GUID 值允许 nil，业务身份是否非空由所属类型校验。
结构修改、元素删除 及父对象移动后，必须重新获取受影响的元素视图；
视图不检测悬空地址。视图保留注册表寿命，但不延长容器或元素寿命，调用方负责线程同步。

反射 JSON 缺失非必填容器字段时保留默认值并验证现有对象元素；显式字段替换整个容器内容。
未知对象字段沿用既有忽略规则；关联对象的每个键都是数据，重复键拒绝。
哈希容器按键排序编码，诊断包含索引或 JSON Pointer 转义键，如 `/groups/a~1b/0`。
容器和对象共用 64 层复合值深度限制，标量叶子不额外计层。
对象元素自定义 JSON codec 保持优先。解码失败释放临时根对象，不返回半成品，
但可写视图本身不提供事务回滚，用户回调的外部副作用也不会回滚。

## 操作表与自定义适配

内部 `detail::type_ops` 保存类型身份、赋值操作及一个类别 variant；不再携带全部容器回调。
静态类别表分为 `scalar_ops`、`sequence_ops`、`associative_ops`、`optional_ops`、`variant_ops` 和 `reference_ops`，
位于 `src/engine/core/meta/ops/`，每个 C++ 类型共享静态表，不随对象分配。
ref 表保留共享引用保活和赋值语义；目前只内置适配引擎 `ref<T>`，
不自动接管裸指针、shared_ptr 或 unique_ptr，也不新增动态操作注册中心。

在第一次生成该类型的反射操作表前专门化 `meta::sequence_traits<T>` 或
`meta::associative_traits<T>`；特化应放在所有使用该类型反射的翻译单元可见的同一头文件中。
类型不能同时声明为序列和映射。对象属性、名称与 GUID 仍由 type_builder 注册。

| traits | 必需成员 | 可选成员 |
| --- | --- | --- |
| sequence_traits | enabled=true、element、size(const T&)、at(const T&, size_t) | at_mut(T&, size_t)、resize(T&, size_t)、fixed_size=true、replace(T&, const vector<element>&) |
| associative_traits | enabled=true、element、size(const T&)、keys(const T&)、find(const T&, string_view) | find_mut、insert、erase、clear |

size 返回 size_t；keys 返回自有 vector<string>，操作表统一按键排序。at/find 返回 const element*，
可写访问与 insert 返回 element*；find 未找到返回 nullptr。insert 保留已有值，erase 返回是否删除，
resize/clear 返回 void。回调返回真实、稳定的元素地址，不得返回临时对象或代理引用。
可选 replace 返回 error：先在临时 vector 中完成解码，再由回调整组校验并替换。
回调必须保证失败不改变原集合。存在 replace 时 JSON 解码优先使用该入口；
可写序列视图通过 supports_replacement()/replace() 暴露此能力，无需开放 at_mut 或 resize。
component_descriptions 使用此方式拒绝空组件和重复类型，保留多态引用的通用数组格式。
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

适配器可抛出异常，const_instance/容器视图边界将 bad_alloc 映射为 out_of_memory，其他异常按现有接口
规则返回错误；不承诺回滚自定义回调产生的修改。只读适配可用于遍历和编码，不能因此推断其支持解码。
容器结构修改后的借用视图失效规则保持不变。

## 稳定元素地址

`sequence_identity_traits<Sequence>` 是独立的可选适配，提供 `enabled=true` 与
`static guid identity(const element&)`。不会根据成员名自动推测身份，也不为普通 vector 生成 ID。
现有 `vector<game_object_description>` 使用对象 UUID；声明须在首次生成操作表前可见。

视图提供 `has_identity()`、`identity(index)` 与 `find(guid)`；find 每次检查序列身份并线性查找，
空身份或重复身份返回 invalid_argument，目标消失返回 not_found，不回退位置下标。
它不缓存元素指针。结构修改后重新从根解析路径，原借用视图的失效规则仍然适用。
类型视图提供 `is_fixed_sequence()` 和 `has_sequence_identity()`，可在没有对象实例时检查地址能力。

`resolve_path(root, path)` 支持字段 ID、下标、映射字符串键与元素 GUID。
`resolve_path(root, path, true)` 用于持久化地址，拒绝可变序列的下标步骤；固定长度序列仍允许下标。
路径展示使用 `property_path_text`，其字符串不作为可解析的持久化格式。
Prefab 的目标定位与无身份列表快照规则见[场景文件格式](scene-format.md)。

## Variant 分支反射

`std::variant<T...>` 使用 `type_kind::variant` 和独立 `variant_ops`。
`as_variant()` 返回借用视图，提供 alternative_count、alternative(index)、index、value 和 names；
可写视图增加 select(index)，默认构造并替换当前分支。同一分支再次 select 也重置内容。
不支持默认构造的分支返回 unsupported，越界返回 invalid_argument。
valueless_by_exception 状态下 index/value 返回 invalid_state；select 可用于恢复。
构造异常映射为 out_of_memory 或 internal，不承诺恢复旧分支。
切换分支、赋值或移动宿主后，必须重新获取内部视图。

文件标签必须在第一次使用该类型的 Meta 操作前显式配置，在所有使用它的翻译单元保持一致：

```cpp
using choice = std::variant<float, std::string>;
namespace gneiss::engine::meta {
template <> struct variant_case_traits<choice> {
  static constexpr std::array<std::string_view, 2> names{"number", "text"};
};
}
```

names 与 C++ 分支位置一一对应，编译期检查数量、非空和唯一性。
JSON 保存 `{"$kind":"text","$content":"hello"}`，不保存分支下标或编译器类型名。
调整 C++ 分支顺序时同步调整 names，已保存标签不变；同一个 variant 类型的所有属性共用标签。
未配置标签时仍可使用视图，JSON 编解码返回 unsupported。
未知标签、缺失字段及多余包装字段返回 invalid_argument；诊断通过 /$kind 或 /$content 定位。
注册属性冻结时检查所有候选分支；内存视图只在实际访问对象分支时检查注册。
monostate 使用 empty 类型类别，内容保存为 null。
Variant 本身不支持 read_value，须先 value() 取得当前分支。此能力不自动增加编辑器切换控件。

`meta::property_step` 显式使用 property/index/key/element 标签；property_id 作为范围受限的 uint32 数值编解码。
`engine::value` 使用同一 variant 包装，variant_traits 将访问转交给 payload。
自定义包装类型可提供 storage_type 与可变/只读 storage()；分支名仍通过 variant_case_traits 配置。
