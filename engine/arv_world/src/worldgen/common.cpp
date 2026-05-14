#include "arv/world/worldgen/common.hpp"

#include "arv/asset/pack.hpp"

#include <cctype>

namespace arv::world::worldgen {

std::string WorldGenDiagnostic::describe() const {
  std::string output;
  if (!resource_id.empty()) {
    output += resource_id;
    output += ": ";
  }
  if (!field_path.empty()) {
    output += field_path;
    output += " ";
  }
  output += message;
  return output;
}

std::optional<GenerationStage> generation_stage_from_string(std::string_view value) noexcept {
  if (value == "underground_ores") {
    return GenerationStage::UndergroundOres;
  }
  if (value == "underground_decoration") {
    return GenerationStage::UndergroundDecoration;
  }
  if (value == "surface_decoration") {
    return GenerationStage::SurfaceDecoration;
  }
  if (value == "underwater_decoration") {
    return GenerationStage::UnderwaterDecoration;
  }
  if (value == "structures") {
    return GenerationStage::Structures;
  }
  return std::nullopt;
}

std::string_view generation_stage_name(GenerationStage stage) noexcept {
  switch (stage) {
    case GenerationStage::UndergroundOres:
      return "underground_ores";
    case GenerationStage::UndergroundDecoration:
      return "underground_decoration";
    case GenerationStage::SurfaceDecoration:
      return "surface_decoration";
    case GenerationStage::UnderwaterDecoration:
      return "underwater_decoration";
    case GenerationStage::Structures:
      return "structures";
  }
  return "surface_decoration";
}

bool valid_resource_id(std::string_view value) noexcept {
  return asset::ResourceID::valid(value);
}

bool valid_color(std::string_view value) noexcept {
  if (value.size() != 7U || value.front() != '#') {
    return false;
  }
  for (usize index = 1; index < value.size(); ++index) {
    const unsigned char c = static_cast<unsigned char>(value[index]);
    if (std::isxdigit(c) == 0) {
      return false;
    }
  }
  return true;
}

} // namespace arv::world::worldgen
