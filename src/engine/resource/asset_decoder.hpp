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
  [[nodiscard]] virtual engine::error decode(const std::vector<std::byte>& bytes,
                                             std::shared_ptr<const void>& output,
                                             meta::serialization_diagnostic& diagnostic) const = 0;
};

/** 有类型解码函数；失败对象由适配器丢弃，可并发处理不同输入。 */
template <class T>
using asset_decode_function = error (*)(std::span<const std::byte>, T&,
                                        meta::serialization_diagnostic&);
namespace detail {
template <class T> class typed_asset_decoder final : public asset_decoder {
public:
  typed_asset_decoder(asset_decode_function<T> function, std::size_t limit,
                      meta::registry_ref registry = {}, meta::serialization_binding binding = {})
      : function_(function), limit_(limit), registry_(std::move(registry)), binding_(binding) {}
  type_id target_type() const noexcept override { return runtime_type_of<T>(); }
  std::size_t maximum_bytes() const noexcept override { return limit_; }
  error decode(const std::vector<std::byte>& bytes, std::shared_ptr<const void>& output,
               meta::serialization_diagnostic& diagnostic) const override {
    auto pending = std::make_shared<T>();
    const auto status = function_ ? function_(bytes, *pending, diagnostic)
                                  : binding_.decode(bytes, pending.get(), diagnostic);
    if (status == error::success)
      output = std::move(pending);
    return status;
  }

private:
  asset_decode_function<T> function_{};
  std::size_t limit_{};
  meta::registry_ref registry_;
  meta::serialization_binding binding_{};
};
template <class T>
error make_reflected_decoder(const meta::registry_ref& registry, std::size_t limit,
                             std::shared_ptr<const asset_decoder>& output) {
  if (!registry)
    return error::unsupported;
  meta::serialization_binding binding;
  const auto status = find_serialization(registry, runtime_type_of<T>(), binding);
  if (status == error::not_found)
    return error::unsupported;
  if (status.failed())
    return status;
  if (!binding.decode)
    return error::unsupported;
  if constexpr (std::is_default_constructible_v<T> && !std::is_const_v<T>) {
    if constexpr (requires { T::maximum_source_bytes; })
      limit = std::min(limit, static_cast<std::size_t>(T::maximum_source_bytes));
    output = std::make_shared<typed_asset_decoder<T>>(nullptr, limit, registry, binding);
    return error::success;
  } else {
    return error::unsupported;
  }
}
}
}
