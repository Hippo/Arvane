#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/id.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace arv::asset {

struct TextureResourceTag {};

using TextureResourceID = ID<TextureResourceTag, u32>;

struct TextureResourceHandle {
  TextureResourceID id{};

  [[nodiscard]] static constexpr TextureResourceHandle invalid() noexcept {
    return {};
  }

  [[nodiscard]] constexpr bool valid() const noexcept {
    return id.value != 0;
  }

  constexpr bool operator==(const TextureResourceHandle&) const = default;
};

struct AssetID {
  std::string value{};

  [[nodiscard]] bool empty() const noexcept {
    return value.empty();
  }

  constexpr bool operator==(const AssetID&) const = default;
};

enum class AssetKind : u8 {
  Texture,
  ShaderSource,
  Font,
};

struct AssetError {
  std::string asset_id{};
  std::filesystem::path path{};
  std::string operation{};
  std::string message{};
};

struct Image {
  u32 width{};
  u32 height{};
  u32 channels{4};
  std::vector<u8> pixels{};

  [[nodiscard]] bool empty() const noexcept {
    return width == 0 || height == 0 || pixels.empty();
  }
};

struct TextureResource {
  AssetID asset_id{};
  std::filesystem::path path{};
  Image image{};
  bool fallback{};
  u32 version{1};
};

using ImageResult = Expected<Image, AssetError>;
using TextAssetResult = Expected<std::string, AssetError>;
using TextureLoadResult = Expected<TextureResourceHandle, AssetError>;

[[nodiscard]] bool is_valid_asset_id(std::string_view id) noexcept;
[[nodiscard]] std::optional<std::pair<std::string_view, std::string_view>> split_asset_id(
    std::string_view id) noexcept;

[[nodiscard]] Image make_missing_texture_image(u32 size = 16);

class ResourceManager {
public:
  explicit ResourceManager(std::filesystem::path asset_root);

  [[nodiscard]] const std::filesystem::path& asset_root() const noexcept {
    return asset_root_;
  }

  [[nodiscard]] std::filesystem::path resolve_asset_path(
      const AssetID& asset_id,
      AssetKind kind) const;

  [[nodiscard]] TextAssetResult load_text(const AssetID& asset_id, AssetKind kind) const;

  [[nodiscard]] TextureLoadResult load_texture(const AssetID& asset_id);

  [[nodiscard]] TextureResourceHandle missing_texture();

  [[nodiscard]] const TextureResource* texture(TextureResourceHandle handle) const noexcept;

  [[nodiscard]] usize texture_count() const noexcept {
    return textures_.size() > 0 ? textures_.size() - 1U : 0U;
  }

private:
  [[nodiscard]] TextureResourceHandle publish_texture(TextureResource resource);

  std::filesystem::path asset_root_{};
  std::vector<TextureResource> textures_{{}};
  std::unordered_map<std::string, TextureResourceHandle> texture_cache_{};
};

[[nodiscard]] ImageResult load_png_image(
    const std::filesystem::path& path,
    const AssetID& asset_id);

} // namespace arv::asset

template<>
struct std::hash<arv::asset::TextureResourceHandle> {
  std::size_t operator()(const arv::asset::TextureResourceHandle& handle) const noexcept {
    return std::hash<arv::asset::TextureResourceID>{}(handle.id);
  }
};

template<>
struct std::hash<arv::asset::AssetID> {
  std::size_t operator()(const arv::asset::AssetID& id) const noexcept {
    return std::hash<std::string>{}(id.value);
  }
};
