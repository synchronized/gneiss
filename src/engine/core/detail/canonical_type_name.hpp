// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CAORS contributors
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ENGINE_DETAIL_CANONICAL_TYPE_NAME_HPP_
#define GNEISS_ENGINE_DETAIL_CANONICAL_TYPE_NAME_HPP_

#include <string_view>
#include <type_traits>

namespace gneiss::engine::detail {

namespace canonical_type_name_detail {

[[nodiscard]] constexpr std::string_view
extract_msvc_type_name(std::string_view signature) noexcept {
  constexpr std::string_view marker = "canonical_cpp_type_name_impl<";
  constexpr std::string_view suffix = ">(void)";

  const std::size_t marker_pos = signature.find(marker);
  if (marker_pos == std::string_view::npos) {
    return {};
  }

  const std::size_t begin = marker_pos + marker.size();
  const std::size_t end = signature.rfind(suffix);
  if (end == std::string_view::npos || end <= begin) {
    return {};
  }
  return signature.substr(begin, end - begin);
}

[[nodiscard]] constexpr std::string_view
extract_pretty_function_type_name(std::string_view signature) noexcept {
  constexpr std::string_view marker = "T = ";

  const std::size_t marker_pos = signature.find(marker);
  if (marker_pos == std::string_view::npos) {
    return {};
  }

  const std::size_t begin = marker_pos + marker.size();
  const std::size_t bracket_end = signature.rfind(']');
  const std::size_t alias_end = signature.find(';', begin);
  const std::size_t end =
      alias_end != std::string_view::npos && alias_end < bracket_end ? alias_end : bracket_end;
  if (end == std::string_view::npos || end <= begin) {
    return {};
  }
  return signature.substr(begin, end - begin);
}

} // namespace canonical_type_name_detail

template <typename T>
[[nodiscard]] constexpr std::string_view canonical_cpp_type_name_impl() noexcept {
#if defined(__clang__)
  constexpr std::string_view signature = __PRETTY_FUNCTION__;
  constexpr std::string_view name =
      canonical_type_name_detail::extract_pretty_function_type_name(signature);
#elif defined(_MSC_VER)
  constexpr std::string_view signature = __FUNCSIG__;
  constexpr std::string_view name = canonical_type_name_detail::extract_msvc_type_name(signature);
#elif defined(__GNUC__)
  constexpr std::string_view signature = __PRETTY_FUNCTION__;
  constexpr std::string_view name =
      canonical_type_name_detail::extract_pretty_function_type_name(signature);
#else
#error "当前编译器尚未实现 canonical C++ type name 提取"
#endif

  static_assert(!name.empty(), "无法从编译器函数签名中提取 canonical C++ type name");
  return name;
}

/**
 * @brief 返回当前工具链生成的规范 C++ 类型名称。
 *
 * 返回值引用编译器生成的静态函数签名文本。该名称仅用于相同工具链和 ABI 配置下的运行期
 * 类型身份合并，不是显示名称或持久化名称。
 */
template <typename T> [[nodiscard]] constexpr std::string_view canonical_cpp_type_name() noexcept {
  using type = std::remove_cvref_t<T>;
  return canonical_cpp_type_name_impl<type>();
}

} // namespace gneiss::engine::detail

#endif
