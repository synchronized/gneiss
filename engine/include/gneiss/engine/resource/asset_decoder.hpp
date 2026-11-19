// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <algorithm>
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/engine/core/base/type_id.hpp>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <memory>
#include <vector>

namespace gneiss {
/** CPU 字节解码契约；只解析字节，不创建 GPU 或场景对象。
 * 同一 Decoder 可并发解码；输出失败时不发布。类型无需继承资源基类或注册字段反射。 */
class asset_decoder {
public:
  virtual ~asset_decoder() = default;
  [[nodiscard]] virtual type_id target_type() const noexcept = 0;
  [[nodiscard]] virtual result<std::shared_ptr<const void>, serialization::diagnostic>
  decode(const std::vector<std::byte>& bytes) const = 0;
  virtual result<std::shared_ptr<const void>, serialization::diagnostic>
  decode(const serialization::read_context&, const std::vector<std::byte>& bytes) const {
    return decode(bytes);
  }
};

/** 有类型解码函数；失败对象由适配器丢弃，可并发处理不同输入。 */
template <class T>
using asset_decode_function = result<T, serialization::diagnostic> (*)(std::span<const std::byte>);
namespace detail {
template <class T> class typed_asset_decoder final : public asset_decoder {
public:
  explicit typed_asset_decoder(asset_decode_function<T> function) : function_(function) {}
  type_id target_type() const noexcept override { return runtime_type_of<T>(); }
  result<std::shared_ptr<const void>, serialization::diagnostic>
  decode(const std::vector<std::byte>& bytes) const override {
    serialization::read_context context;
    return decode(context, bytes);
  }
  result<std::shared_ptr<const void>, serialization::diagnostic>
  decode(const serialization::read_context& context,
         const std::vector<std::byte>& bytes) const override {
    if (function_ || asset_file::format_traits<T>::enabled) {
      auto decoded = [&]() -> result<T, serialization::diagnostic> {
        if (function_)
          return function_(bytes);
        if constexpr (asset_file::format_traits<T>::enabled)
          return asset_file::deserialize<T>(context, bytes);
        return serialization::diagnostic{.error = error::unsupported};
      }();
      if (!decoded)
        return std::move(decoded).error();
      return std::shared_ptr<const void>{std::make_shared<T>(std::move(*decoded))};
    }
    auto decoded = serialization::json::decode<T>(context, bytes);
    if (!decoded)
      return std::move(decoded).error();
    return std::shared_ptr<const void>{std::make_shared<T>(std::move(*decoded))};
  }

private:
  asset_decode_function<T> function_{};
};
template <class T> result<std::shared_ptr<const asset_decoder>> make_reflected_decoder() {
  auto type = meta::type_of<T>();
  if (!type)
    return error::unsupported;
  if (type.kind() == meta::type_kind::object) {
    auto binding = serialization::find_policies(runtime_type_of<T>());
    if (!binding)
      return binding.error() == error::not_found ? error::unsupported : binding.error();
    auto capability = type.template capability<meta::cap::object>();
    if (!binding->content.decode && !capability.can_construct())
      return error::unsupported;
  }
  if constexpr (std::is_move_constructible_v<T> && !std::is_const_v<T>) {
    return std::shared_ptr<const asset_decoder>{std::make_shared<typed_asset_decoder<T>>(nullptr)};
  } else {
    return error::unsupported;
  }
}
}
}
