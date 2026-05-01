#include "arv/asset/resource.hpp"

#include <algorithm>
#include <fstream>
#include <utility>
#include "arv/core/types.hpp"
#include "arv/core/log.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace arv::asset {
namespace {

[[nodiscard]] bool valid_namespace_char(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

[[nodiscard]] bool valid_path_char(char c) noexcept {
  return valid_namespace_char(c) || c == '/' || c == '.';
}

[[nodiscard]] std::filesystem::path path_with_extension(
    std::filesystem::path path,
    std::string_view extension) {
  if (path.extension().empty()) {
    path += extension;
  }
  return path;
}

} // namespace

bool is_valid_asset_id(std::string_view id) noexcept {
  const auto split = split_asset_id(id);
  if (!split.has_value()) {
    return false;
  }

  const auto [name_space, path] = split.value();
  if (name_space.empty() || path.empty() || path.front() == '/' || path.back() == '/') {
    return false;
  }

  return std::all_of(name_space.begin(), name_space.end(), valid_namespace_char) &&
         std::all_of(path.begin(), path.end(), valid_path_char) &&
         path.find("..") == std::string_view::npos;
}

std::optional<std::pair<std::string_view, std::string_view>> split_asset_id(
    std::string_view id) noexcept {
  const usize separator = id.find(':');
  if (separator == std::string_view::npos || separator == 0 || separator + 1 >= id.size()) {
    return std::nullopt;
  }
  return std::pair{id.substr(0, separator), id.substr(separator + 1)};
}

Image make_missing_texture_image(u32 size) {
  Image image;
  image.width = size;
  image.height = size;
  image.channels = 4;
  image.pixels.resize(static_cast<usize>(size) * static_cast<usize>(size) * 4U);

  for (u32 y = 0; y < size; ++y) {
    for (u32 x = 0; x < size; ++x) {
      const bool magenta = ((x / 4U) + (y / 4U)) % 2U == 0U;
      const usize offset = (static_cast<usize>(y) * size + x) * 4U;
      image.pixels[offset + 0U] = magenta ? 255U : 0U;
      image.pixels[offset + 1U] = 0U;
      image.pixels[offset + 2U] = magenta ? 255U : 0U;
      image.pixels[offset + 3U] = 255U;
    }
  }

  return image;
}

ResourceManager::ResourceManager(std::filesystem::path asset_root)
    : asset_root_(std::move(asset_root)) {
}

std::filesystem::path ResourceManager::resolve_asset_path(
    const AssetID& asset_id,
    AssetKind kind) const {
  const auto split = split_asset_id(asset_id.value);
  if (!split.has_value()) {
    return asset_root_ / asset_id.value;
  }

  const auto [name_space, path] = split.value();
  std::filesystem::path relative;
  std::string_view extension;
  switch (kind) {
    case AssetKind::Texture:
      if (path.starts_with("texture/")) {
        relative = std::filesystem::path{"textures"} /
                   std::filesystem::path{std::string{path.substr(std::string_view{"texture/"}.size())}};
      } else {
        relative = std::filesystem::path{std::string{path}};
      }
      extension = ".png";
      break;
    case AssetKind::ShaderSource:
      if (path.starts_with("shader/")) {
        relative = std::filesystem::path{"shaders"} /
                   std::filesystem::path{std::string{path.substr(std::string_view{"shader/"}.size())}};
      } else {
        relative = std::filesystem::path{std::string{path}};
      }
      extension = "";
      break;
    case AssetKind::Font:
      if (path.starts_with("font/")) {
        relative = std::filesystem::path{"fonts"} /
                   std::filesystem::path{std::string{path.substr(std::string_view{"font/"}.size())}};
      } else {
        relative = std::filesystem::path{std::string{path}};
      }
      extension = ".ttf";
      break;
    case AssetKind::Model:
      if (path.starts_with("model/")) {
        relative = std::filesystem::path{"models"} /
                   std::filesystem::path{std::string{path.substr(std::string_view{"model/"}.size())}};
      } else {
        relative = std::filesystem::path{std::string{path}};
      }
      extension = ".json";
      break;
  }

  if (name_space != "arvane") {
    relative = std::filesystem::path{std::string{name_space}} / relative;
  }

  return path_with_extension(asset_root_ / relative, extension);
}

TextAssetResult ResourceManager::load_text(const AssetID& asset_id, AssetKind kind) const {
  if (!is_valid_asset_id(asset_id.value)) {
    return TextAssetResult::failure({asset_id.value, {}, "resolve text asset", "invalid asset ID"});
  }

  const std::filesystem::path path = resolve_asset_path(asset_id, kind);
  std::ifstream file(path);
  arv::core::info("assets", "loading text asset '{}' from '{}'", asset_id.value, path);
  if (!file) {
    return TextAssetResult::failure({asset_id.value, path, "read text asset", "file could not be opened"});
  }

  std::string text;
  file.seekg(0, std::ios::end);
  text.resize(static_cast<usize>(file.tellg()));
  file.seekg(0, std::ios::beg);
  file.read(text.data(), static_cast<std::streamsize>(text.size()));
  if (!file && !file.eof()) {
    return TextAssetResult::failure({asset_id.value, path, "read text asset", "file read failed"});
  }

  return TextAssetResult::success(std::move(text));
}

TextureLoadResult ResourceManager::load_texture(const AssetID& asset_id) {
  if (!is_valid_asset_id(asset_id.value)) {
    return TextureLoadResult::failure({asset_id.value, {}, "load texture", "invalid asset ID"});
  }

  const auto found = texture_cache_.find(asset_id.value);
  if (found != texture_cache_.end()) {
    return TextureLoadResult::success(found->second);
  }

  const std::filesystem::path path = resolve_asset_path(asset_id, AssetKind::Texture);
  auto image = load_png_image(path, asset_id);
  if (!image) {
    TextureResource fallback;
    fallback.asset_id = asset_id;
    fallback.path = path;
    fallback.image = make_missing_texture_image();
    fallback.fallback = true;
    return TextureLoadResult::success(publish_texture(std::move(fallback)));
  }

  TextureResource resource;
  resource.asset_id = asset_id;
  resource.path = path;
  resource.image = std::move(image.value());
  return TextureLoadResult::success(publish_texture(std::move(resource)));
}

TextureResourceHandle ResourceManager::missing_texture() {
  constexpr std::string_view MissingID = "arvane:texture/missing";
  const auto found = texture_cache_.find(std::string{MissingID});
  if (found != texture_cache_.end()) {
    return found->second;
  }

  TextureResource resource;
  resource.asset_id.value = std::string{MissingID};
  resource.image = make_missing_texture_image();
  resource.fallback = true;
  return publish_texture(std::move(resource));
}

const TextureResource* ResourceManager::texture(TextureResourceHandle handle) const noexcept {
  if (!handle.valid() || handle.id.value >= textures_.size()) {
    return nullptr;
  }
  return &textures_[handle.id.value];
}

TextureResourceHandle ResourceManager::publish_texture(TextureResource resource) {
  const TextureResourceHandle handle{TextureResourceID{static_cast<u32>(textures_.size())}};
  texture_cache_[resource.asset_id.value] = handle;
  textures_.push_back(std::move(resource));
  return handle;
}

ImageResult load_png_image(const std::filesystem::path& path, const AssetID& asset_id) {
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* loaded = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
  if (loaded == nullptr) {
    const char* reason = stbi_failure_reason();
    return ImageResult::failure({
        asset_id.value,
        path,
        "decode PNG texture",
        reason == nullptr ? "unknown stb_image decode failure" : std::string{reason}});
  }
  if (width <= 0 || height <= 0) {
    stbi_image_free(loaded);
    return ImageResult::failure({asset_id.value, path, "decode PNG texture", "image has invalid dimensions"});
  }

  Image image;
  image.width = static_cast<u32>(width);
  image.height = static_cast<u32>(height);
  image.channels = 4;
  image.pixels.assign(loaded, loaded + static_cast<usize>(width) * static_cast<usize>(height) * 4U);
  stbi_image_free(loaded);
  return ImageResult::success(std::move(image));
}

} // namespace arv::asset
