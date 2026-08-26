// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.h>
#include <gneiss/asset.h>

#include <stdint.h>
#include <string.h>

static gneiss_result validate(const char* uri) {
  return gneiss_asset_uri_validate(uri, (uint64_t)strlen(uri));
}

int main(void) {
  static const unsigned char invalid_utf8[] = {'a', 's', 's',  'e',  't', ':',
                                               '/', '/', 0xC0, 0xAF, 0};
  if (validate("asset://models/triangle.mesh") != GNEISS_SUCCESS ||
      validate("asset://纹理/石头.png") != GNEISS_SUCCESS ||
      validate("file://models/triangle.mesh") != GNEISS_ERROR_INVALID_ARGUMENT ||
      validate("asset://") != GNEISS_ERROR_INVALID_ARGUMENT ||
      validate("asset:///root") != GNEISS_ERROR_INVALID_ARGUMENT ||
      validate("asset://models//triangle.mesh") != GNEISS_ERROR_INVALID_ARGUMENT ||
      validate("asset://models/./triangle.mesh") != GNEISS_ERROR_INVALID_ARGUMENT ||
      validate("asset://models/../secret") != GNEISS_ERROR_INVALID_ARGUMENT ||
      validate("asset://C:/secret") != GNEISS_ERROR_INVALID_ARGUMENT ||
      validate("asset://models\\triangle.mesh") != GNEISS_ERROR_INVALID_ARGUMENT ||
      validate("asset://models/a%2Fb") != GNEISS_ERROR_INVALID_ARGUMENT ||
      validate((const char*)invalid_utf8) != GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss_asset_uri_validate(NULL, UINT64_C(0)) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 1;
  }

  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  gneiss_application application = GNEISS_NULL_APPLICATION;
  desc.asset_root = ".";
  desc.asset_root_length = UINT32_C(1);
  if (gneiss_application_create(&desc, &application) != GNEISS_SUCCESS ||
      gneiss_application_destroy(application) != GNEISS_SUCCESS) {
    return 2;
  }
  desc.asset_root = NULL;
  if (gneiss_application_create(&desc, &application) != GNEISS_ERROR_INVALID_ARGUMENT ||
      application != GNEISS_NULL_APPLICATION) {
    return 3;
  }
  return 0;
}
