#pragma once

#include "arv/data.hpp"
#include "arv/result.hpp"
#include "arv/io/codec.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace arv {

namespace detail {

[[nodiscard]] inline std::string codec_child_path(
    std::string_view parent,
    std::string_view child) {
  if (parent.empty()) {
    return std::string{child};
  }
  std::string path{parent};
  path.push_back('.');
  path.append(child);
  return path;
}

[[nodiscard]] inline Result codec_encode_vec3_at(
    const DataDocument& document,
    std::string_view path,
    ArvVec3 value) {
  if (Result result = document.set_f64(codec_child_path(path, "x"), value.x); !result) {
    return result;
  }
  if (Result result = document.set_f64(codec_child_path(path, "y"), value.y); !result) {
    return result;
  }
  return document.set_f64(codec_child_path(path, "z"), value.z);
}

[[nodiscard]] inline DecodeResult<ArvVec3> codec_decode_vec3_at(
    const DataDocument& document,
    std::string_view path) {
  ArvVec3 value{};
  auto x = document.f64_value(codec_child_path(path, "x"));
  if (!x) {
    return {x.error(), {}};
  }
  auto y = document.f64_value(codec_child_path(path, "y"));
  if (!y) {
    return {y.error(), {}};
  }
  auto z = document.f64_value(codec_child_path(path, "z"));
  if (!z) {
    return {z.error(), {}};
  }
  value.x = static_cast<float>(x.value());
  value.y = static_cast<float>(y.value());
  value.z = static_cast<float>(z.value());
  return {Result::ok(), value};
}

} // namespace detail

template<>
struct Codec<ArvVec3> {
  static constexpr std::uint32_t schema_version = 1;

  [[nodiscard]] static Result encode(
      const DataDocument& document,
      const ArvVec3& value) {
    return detail::codec_encode_vec3_at(document, {}, value);
  }

  [[nodiscard]] static DecodeResult<ArvVec3> decode(const DataDocument& document) {
    return detail::codec_decode_vec3_at(document, {});
  }
};

template<>
struct Codec<ArvLocation> {
  static constexpr std::uint32_t schema_version = 1;

  [[nodiscard]] static Result encode(
      const DataDocument& document,
      const ArvLocation& value) {
    if (Result result = document.set_f64("x", value.x); !result) {
      return result;
    }
    if (Result result = document.set_f64("y", value.y); !result) {
      return result;
    }
    if (Result result = document.set_f64("z", value.z); !result) {
      return result;
    }
    if (Result result = document.set_f64("yaw_radians", value.yaw_radians);
        !result) {
      return result;
    }
    if (Result result = document.set_f64("pitch_radians", value.pitch_radians);
        !result) {
      return result;
    }
    return value.has_world ? document.set_u64("world", value.world)
                           : document.set_null("world");
  }

  [[nodiscard]] static DecodeResult<ArvLocation> decode(
      const DataDocument& document) {
    ArvLocation value{};
    auto x = document.f64_value("x");
    if (!x) {
      return {x.error(), {}};
    }
    auto y = document.f64_value("y");
    if (!y) {
      return {y.error(), {}};
    }
    auto z = document.f64_value("z");
    if (!z) {
      return {z.error(), {}};
    }
    auto yaw = document.f64_value("yaw_radians");
    if (!yaw) {
      return {yaw.error(), {}};
    }
    auto pitch = document.f64_value("pitch_radians");
    if (!pitch) {
      return {pitch.error(), {}};
    }
    value.x = static_cast<float>(x.value());
    value.y = static_cast<float>(y.value());
    value.z = static_cast<float>(z.value());
    value.yaw_radians = static_cast<float>(yaw.value());
    value.pitch_radians = static_cast<float>(pitch.value());
    const DataKind world_kind = document.kind("world");
    if (world_kind != DataKind::Invalid && world_kind != DataKind::Null) {
      auto world = document.u64_value("world");
      if (!world) {
        return {world.error(), {}};
      }
      value.has_world = true;
      value.world = world.value();
    }
    return {Result::ok(), value};
  }
};

} // namespace arv
