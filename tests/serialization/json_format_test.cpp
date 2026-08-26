// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <yyjson.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace {

yyjson_doc* parse(std::string_view text, yyjson_read_err& error) {
  return yyjson_read_opts(const_cast<char*>(text.data()), text.size(), YYJSON_READ_NOFLAG, nullptr,
                          &error);
}

bool validates_scene_shape() {
  constexpr std::string_view text =
      R"({"format":"gneiss.scene","version":1,"未知字段":true,"exact":9007199254740993,"scale":1.5})";
  yyjson_read_err error{};
  yyjson_doc* document = parse(text, error);
  if (document == nullptr) {
    return false;
  }

  yyjson_val* root = yyjson_doc_get_root(document);
  const bool is_valid =
      yyjson_is_obj(root) && yyjson_obj_size(root) == 5 &&
      std::string_view{yyjson_get_str(yyjson_obj_get(root, "format"))} == "gneiss.scene" &&
      yyjson_get_uint(yyjson_obj_get(root, "version")) == UINT64_C(1) &&
      yyjson_get_uint(yyjson_obj_get(root, "exact")) == UINT64_C(9007199254740993) &&
      yyjson_get_real(yyjson_obj_get(root, "scale")) == 1.5;
  yyjson_doc_free(document);
  return is_valid;
}

bool reports_syntax_position() {
  constexpr std::string_view text = R"({"version":1,})";
  yyjson_read_err error{};
  yyjson_doc* document = parse(text, error);
  if (document != nullptr) {
    yyjson_doc_free(document);
    return false;
  }
  return error.code != YYJSON_READ_SUCCESS && error.pos > 0 && error.msg != nullptr;
}

bool rejects_invalid_utf8() {
  constexpr std::array<char, 10> text{'{', '"', 'x', '"', ':', '"', static_cast<char>(0xC3),
                                      '(', '"', '}'};
  yyjson_read_err error{};
  yyjson_doc* document = yyjson_read_opts(const_cast<char*>(text.data()), text.size(),
                                          YYJSON_READ_NOFLAG, nullptr, &error);
  if (document != nullptr) {
    yyjson_doc_free(document);
    return false;
  }
  return error.code != YYJSON_READ_SUCCESS;
}

} // namespace

int main() {
  return validates_scene_shape() && reports_syntax_position() && rejects_invalid_utf8() ? 0 : 1;
}
