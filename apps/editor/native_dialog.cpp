// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "native_dialog.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <shobjidl.h>
#endif

namespace gneiss::editor {

result select_project_directory(std::filesystem::path& output) noexcept {
#if defined(_WIN32)
  const auto initialize_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const auto should_uninitialize = initialize_result == S_OK || initialize_result == S_FALSE;
  if (FAILED(initialize_result) && initialize_result != RPC_E_CHANGED_MODE) {
    return result::initialization_failed;
  }
  if (initialize_result == RPC_E_CHANGED_MODE) {
    return result::unsupported;
  }

  IFileOpenDialog* dialog = nullptr;
  auto operation =
      CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
  if (SUCCEEDED(operation)) {
    FILEOPENDIALOGOPTIONS options = 0;
    operation = dialog->GetOptions(&options);
    if (SUCCEEDED(operation)) {
      operation = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                     FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT);
    }
  }
  if (SUCCEEDED(operation)) {
    operation = dialog->Show(nullptr);
  }
  IShellItem* item = nullptr;
  if (SUCCEEDED(operation)) {
    operation = dialog->GetResult(&item);
  }
  PWSTR selected_path = nullptr;
  if (SUCCEEDED(operation)) {
    operation = item->GetDisplayName(SIGDN_FILESYSPATH, &selected_path);
  }
  if (SUCCEEDED(operation)) {
    output = selected_path;
  }
  CoTaskMemFree(selected_path);
  if (item != nullptr) {
    item->Release();
  }
  if (dialog != nullptr) {
    dialog->Release();
  }
  if (should_uninitialize) {
    CoUninitialize();
  }
  if (operation == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
    return result::not_ready;
  }
  return SUCCEEDED(operation) ? result::success : result::io;
#else
  (void)output;
  return result::unsupported;
#endif
}

result select_source_asset(std::filesystem::path& output) noexcept {
#if defined(_WIN32)
  const auto initialize_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const auto should_uninitialize = initialize_result == S_OK || initialize_result == S_FALSE;
  if (FAILED(initialize_result) && initialize_result != RPC_E_CHANGED_MODE) {
    return result::initialization_failed;
  }
  if (initialize_result == RPC_E_CHANGED_MODE) {
    return result::unsupported;
  }

  IFileOpenDialog* dialog = nullptr;
  auto operation =
      CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
  if (SUCCEEDED(operation)) {
    constexpr COMDLG_FILTERSPEC filters[] = {{L"glTF 资产 (*.gltf;*.glb)", L"*.gltf;*.glb"}};
    operation = dialog->SetFileTypes(1U, filters);
  }
  if (SUCCEEDED(operation)) {
    FILEOPENDIALOGOPTIONS options = 0;
    operation = dialog->GetOptions(&options);
    if (SUCCEEDED(operation)) {
      operation = dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST |
                                     FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT);
    }
  }
  if (SUCCEEDED(operation)) {
    operation = dialog->Show(nullptr);
  }
  IShellItem* item = nullptr;
  if (SUCCEEDED(operation)) {
    operation = dialog->GetResult(&item);
  }
  PWSTR selected_path = nullptr;
  if (SUCCEEDED(operation)) {
    operation = item->GetDisplayName(SIGDN_FILESYSPATH, &selected_path);
  }
  if (SUCCEEDED(operation)) {
    output = selected_path;
  }
  CoTaskMemFree(selected_path);
  if (item != nullptr) {
    item->Release();
  }
  if (dialog != nullptr) {
    dialog->Release();
  }
  if (should_uninitialize) {
    CoUninitialize();
  }
  if (operation == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
    return result::not_ready;
  }
  return SUCCEEDED(operation) ? result::success : result::io;
#else
  (void)output;
  return result::unsupported;
#endif
}

} // namespace gneiss::editor
