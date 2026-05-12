#include "arv/asset/resource.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <utility>
#include "arv/core/types.hpp"
#include "arv/core/log.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace arv::asset {
namespace {

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
  return ResourceID::valid(id);
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

ResourceManager::ResourceManager(PackManager pack_manager)
    : asset_root_(pack_manager.packs_root()),
      pack_manager_(std::move(pack_manager)) {
}

std::filesystem::path ResourceManager::resolve_asset_path(
    const AssetID& asset_id,
    AssetKind kind) const {
  if (pack_manager_.has_value()) {
    auto resolved = pack_manager_->resolve_asset_path(asset_id.value, kind);
    if (resolved) {
      return resolved.value().path;
    }
    return {};
  }

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
    case AssetKind::Animation:
      if (path.starts_with("animation/")) {
        relative = std::filesystem::path{"animations"} /
                   std::filesystem::path{std::string{path.substr(std::string_view{"animation/"}.size())}};
      } else {
        relative = std::filesystem::path{std::string{path}};
      }
      extension = ".json";
      break;
    case AssetKind::Theme:
      if (path.starts_with("theme/")) {
        relative = std::filesystem::path{"themes"} /
                   std::filesystem::path{std::string{path.substr(std::string_view{"theme/"}.size())}};
      } else {
        relative = std::filesystem::path{std::string{path}};
      }
      extension = ".yaml";
      break;
  }

  if (name_space != "arvane") {
    relative = std::filesystem::path{std::string{name_space}} / relative;
  }

  return path_with_extension(asset_root_ / relative, extension);
}

PackPathResult ResourceManager::resolve_asset(
    const AssetID& asset_id,
    AssetKind kind) const {
  if (pack_manager_.has_value()) {
    return pack_manager_->resolve_asset_path(asset_id.value, kind);
  }
  if (!is_valid_asset_id(asset_id.value)) {
    return PackPathResult::failure(
        {"", "", {}, asset_id.value, "asset resolution", "invalid asset ID"});
  }
  const std::filesystem::path path = resolve_asset_path(asset_id, kind);
  if (!std::filesystem::is_regular_file(path)) {
    return PackPathResult::failure(
        {"", "", path, asset_id.value, "asset resolution", "asset was not found"});
  }
  return PackPathResult::success({0U, {}, {}, {}, path});
}

TextAssetResult ResourceManager::load_text(const AssetID& asset_id, AssetKind kind) const {
  if (!is_valid_asset_id(asset_id.value)) {
    return TextAssetResult::failure({asset_id.value, {}, "resolve text asset", "invalid asset ID"});
  }

  auto resolved = resolve_asset(asset_id, kind);
  if (!resolved) {
    return TextAssetResult::failure({
        asset_id.value,
        resolved.error().file,
        resolved.error().phase,
        resolved.error().describe()});
  }
  const std::filesystem::path path = resolved.value().path;
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

  auto resolved = resolve_asset(asset_id, AssetKind::Texture);
  const std::filesystem::path path = resolved ? resolved.value().path : std::filesystem::path{};
  if (!resolved) {
    TextureResource fallback;
    fallback.asset_id = asset_id;
    fallback.path = path;
    fallback.image = make_missing_texture_image();
    fallback.fallback = true;
    return TextureLoadResult::success(publish_texture(std::move(fallback)));
  }
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

ImageResult decode_png_image_bytes(std::span<const u8> bytes, const AssetID& asset_id) {
  int width = 0;
  int height = 0;
  int channels = 0;
  const int byte_count = static_cast<int>(std::min<usize>(
      bytes.size(),
      static_cast<usize>(std::numeric_limits<int>::max())));
  if (static_cast<usize>(byte_count) != bytes.size()) {
    return ImageResult::failure({
        asset_id.value,
        {},
        "decode PNG bytes",
        "image payload is too large for stb_image"});
  }
  stbi_uc* loaded = stbi_load_from_memory(
      bytes.data(),
      byte_count,
      &width,
      &height,
      &channels,
      4);
  if (loaded == nullptr) {
    const char* reason = stbi_failure_reason();
    return ImageResult::failure({
        asset_id.value,
        {},
        "decode PNG bytes",
        reason == nullptr ? "unknown stb_image decode failure" : std::string{reason}});
  }
  if (width <= 0 || height <= 0) {
    stbi_image_free(loaded);
    return ImageResult::failure({asset_id.value, {}, "decode PNG bytes", "image has invalid dimensions"});
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
