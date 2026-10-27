// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/serialization.hpp"
#include "engine/core/result.hpp"
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
  [[nodiscard]] virtual gneiss::engine::type_id target_type() const noexcept = 0;
  [[nodiscard]] virtual std::size_t maximum_bytes() const noexcept = 0;
  [[nodiscard]] virtual gneiss::engine::result
  decode(const std::vector<std::byte>& bytes, std::shared_ptr<const void>& output,
         gneiss::engine::meta::serialization_diagnostic& diagnostic) const = 0;
};

/** 有类型解码函数；失败对象由适配器丢弃，可并发处理不同输入。 */
template <class T>
using asset_decode_function = result (*)(std::span<const std::byte>, T&,
                                         gneiss::engine::meta::serialization_diagnostic&);
namespace detail {
template <class T> class typed_asset_decoder final : public asset_decoder {
public:
  typed_asset_decoder(asset_decode_function<T> function, std::size_t limit,
                      gneiss::engine::meta::registry_ref registry = {},
                      gneiss::engine::meta::serialization_binding binding = {})
      : function_(function), limit_(limit), registry_(std::move(registry)), binding_(binding) {}
  type_id target_type() const noexcept override { return runtime_type_of<T>(); }
  std::size_t maximum_bytes() const noexcept override { return limit_; }
  result decode(const std::vector<std::byte>& bytes, std::shared_ptr<const void>& output,
                gneiss::engine::meta::serialization_diagnostic& diagnostic) const override {
    auto pending = std::make_shared<T>();
    const auto status = function_ ? function_(bytes, *pending, diagnostic)
                                  : binding_.decode(bytes, pending.get(), diagnostic);
    if (status == result::success)
      output = std::move(pending);
    return status;
  }

private:
  asset_decode_function<T> function_{};
  std::size_t limit_{};
  gneiss::engine::meta::registry_ref registry_;
  gneiss::engine::meta::serialization_binding binding_{};
};
template <class T>
result make_reflected_decoder(const gneiss::engine::meta::registry_ref& registry, std::size_t limit,
                              std::shared_ptr<const asset_decoder>& output) {
  if (!registry)
    return result::unsupported;
  gneiss::engine::meta::serialization_binding binding;
  const auto status = find_serialization(registry, runtime_type_of<T>(), binding);
  if (status == result::not_found)
    return result::unsupported;
  if (status.failed())
    return status;
  if (!binding.decode)
    return result::unsupported;
  if constexpr (std::is_default_constructible_v<T> && !std::is_const_v<T>) {
    if constexpr (requires { T::maximum_source_bytes; })
      limit = std::min(limit, static_cast<std::size_t>(T::maximum_source_bytes));
    output = std::make_shared<typed_asset_decoder<T>>(nullptr, limit, registry, binding);
    return result::success;
  } else {
    return result::unsupported;
  }
}
}
}
