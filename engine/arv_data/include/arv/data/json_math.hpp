#pragma once

#include "arv/core/math.hpp"

#include <nlohmann/json.hpp>

namespace arv::data {

[[nodiscard]] inline nlohmann::json vec3_to_json(Vec3f value) {
  return nlohmann::json::array({value.x, value.y, value.z});
}

[[nodiscard]] inline Vec3f json_to_vec3(const nlohmann::json& node) {
  return {
      node.at(0).get<f32>(),
      node.at(1).get<f32>(),
      node.at(2).get<f32>()};
}

} // namespace arv::data
