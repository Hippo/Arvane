#include "dynamic_library.hpp"

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#include <utility>

namespace arv::plugin::detail {

DynamicLibrary::~DynamicLibrary() {
  close();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
  if (this != &other) {
    close();
    handle_ = std::exchange(other.handle_, nullptr);
  }
  return *this;
}

Expected<DynamicLibrary, PluginError> DynamicLibrary::load(
    const std::filesystem::path& path,
    std::string pack_id) {
#if defined(_WIN32)
  HMODULE handle = LoadLibraryW(path.wstring().c_str());
  if (handle == nullptr) {
    return Expected<DynamicLibrary, PluginError>::failure(
        {std::move(pack_id), "native library load", path, "LoadLibrary failed"});
  }
  return Expected<DynamicLibrary, PluginError>::success(
      DynamicLibrary{reinterpret_cast<void*>(handle)});
#else
  void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    const char* error = dlerror();
    return Expected<DynamicLibrary, PluginError>::failure(
        {std::move(pack_id),
         "native library load",
         path,
         error == nullptr ? std::string{"dlopen failed"} : std::string{error}});
  }
  return Expected<DynamicLibrary, PluginError>::success(DynamicLibrary{handle});
#endif
}

void* DynamicLibrary::symbol(std::string_view name, std::string& error_out) const noexcept {
  if (handle_ == nullptr) {
    error_out = "library is not loaded";
    return nullptr;
  }

#if defined(_WIN32)
  FARPROC symbol = GetProcAddress(
      reinterpret_cast<HMODULE>(handle_),
      std::string{name}.c_str());
  if (symbol == nullptr) {
    error_out = "GetProcAddress failed";
    return nullptr;
  }
  return reinterpret_cast<void*>(symbol);
#else
  dlerror();
  void* symbol = dlsym(handle_, std::string{name}.c_str());
  const char* error = dlerror();
  if (error != nullptr) {
    error_out = error;
    return nullptr;
  }
  return symbol;
#endif
}

void DynamicLibrary::close() noexcept {
  if (handle_ == nullptr) {
    return;
  }
#if defined(_WIN32)
  FreeLibrary(reinterpret_cast<HMODULE>(handle_));
#else
  dlclose(handle_);
#endif
  handle_ = nullptr;
}

} // namespace arv::plugin::detail
