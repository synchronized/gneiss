// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/error.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/core/type_id.hpp"
#include <algorithm>
#include <memory>
#include <vector>

namespace gneiss::engine {
/** CPU 字节解码契约；只解析字节，不创建 GPU 或场景对象。
 * 同一 Decoder 可并发解码；输出失败时不发布。类型无需继承资源基类或注册字段反射。 */
class asset_decoder {
public:
  virtual ~asset_decoder() = default;
  [[nodiscard]] virtual type_id target_type() const noexcept = 0;
  [[nodiscard]] virtual std::size_t maximum_bytes() const noexcept = 0;
  [[nodiscard]] virtual result<std::shared_ptr<const void>, meta::serialization_diagnostic>
  decode(const std::vector<std::byte>& bytes) const = 0;
};

/** 有类型解码函数；失败对象由适配器丢弃，可并发处理不同输入。 */
template <class T>
using asset_decode_function =
    result<T, meta::serialization_diagnostic> (*)(std::span<const std::byte>);
namespace detail {
template <class T> class typed_asset_decoder final : public asset_decoder {
public:
  typed_asset_decoder(asset_decode_function<T> function, std::size_t limit)
      : function_(function), limit_(limit) {}
  type_id target_type() const noexcept override { return runtime_type_of<T>(); }
  std::size_t maximum_bytes() const noexcept override { return limit_; }
  result<std::shared_ptr<const void>, meta::serialization_diagnostic>
  decode(const std::vector<std::byte>& bytes) const override try {
    if (function_) {
      auto decoded = function_(bytes);
      if (!decoded)
        return std::move(decoded).error();
      return std::shared_ptr<const void>{std::make_shared<T>(std::move(*decoded))};
    }
    auto decoded = meta::deserialize_object(runtime_type_of<T>(), bytes);
    if (!decoded)
      return std::move(decoded).error();
    if (!*decoded)
      return meta::serialization_diagnostic{.error = error::internal};
    return std::shared_ptr<const void>{std::move(*decoded)};
  } catch (const std::bad_alloc&) {
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  } catch (...) {
    return meta::serialization_diagnostic{.error = error::internal};
  }

private:
  asset_decode_function<T> function_{};
  std::size_t limit_{};
};
template <class T>
result<std::shared_ptr<const asset_decoder>> make_reflected_decoder(std::size_t limit) {
  auto binding = meta::find_serialization(runtime_type_of<T>());
  if (!binding)
    return binding.error() == error::not_found ? error::unsupported : binding.error();
  if (!binding->decode && !binding->reflected_json)
    return error::unsupported;
  if constexpr (std::is_move_constructible_v<T> && !std::is_const_v<T>) {
    if constexpr (requires { T::maximum_source_bytes; })
      limit = std::min(limit, static_cast<std::size_t>(T::maximum_source_bytes));
    return std::shared_ptr<const asset_decoder>{
        std::make_shared<typed_asset_decoder<T>>(nullptr, limit)};
  } else {
    return error::unsupported;
  }
}
}
}
