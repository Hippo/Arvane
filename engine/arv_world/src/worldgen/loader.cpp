#include "arv/world/worldgen/loader.hpp"

#include "arv/blueprint/blueprint.hpp"
#include "arv/io/data_codecs.hpp"

#include <fstream>
#include <sstream>
#include <utility>

namespace arv::world::worldgen {
namespace {

struct Parser {
  std::string id{};
  WorldGenDiagnostic error{};

  [[nodiscard]] bool fail(std::string field, std::string message) {
    error = {id, std::move(field), std::move(message)};
    return false;
  }

  [[nodiscard]] const io::DataValue* member(
      const io::DataValue& root,
      std::string_view field) const noexcept {
    return root.get(field);
  }

  [[nodiscard]] bool require_object(
      const io::DataValue& value,
      std::string_view field) {
    return value.kind() == io::DataKind::Object ||
           fail(std::string{field}, "must be an object");
  }

  [[nodiscard]] bool read_string(
      const io::DataValue& root,
      std::string_view field,
      std::string& out,
      bool required = true) {
    const io::DataValue* node = member(root, field);
    if (node == nullptr) {
      return !required || fail(std::string{field}, "is required");
    }
    if (node->kind() != io::DataKind::String) {
      return fail(std::string{field}, "must be a string");
    }
    out = std::string{node->as_string_or({})};
    return true;
  }

  [[nodiscard]] bool read_ref(
      const io::DataValue& root,
      std::string_view field,
      ResourceRef& out,
      bool required = true) {
    std::string value;
    if (!read_string(root, field, value, required)) {
      return false;
    }
    if (value.empty()) {
      return !required;
    }
    if (!valid_resource_id(value)) {
      return fail(std::string{field}, "must be a valid resource ID");
    }
    out.id = std::move(value);
    return true;
  }

  [[nodiscard]] bool read_i32(
      const io::DataValue& root,
      std::string_view field,
      i32& out,
      bool required = true) {
    const io::DataValue* node = member(root, field);
    if (node == nullptr) {
      return !required || fail(std::string{field}, "is required");
    }
    if (node->kind() != io::DataKind::I64 && node->kind() != io::DataKind::U64) {
      return fail(std::string{field}, "must be an integer");
    }
    out = static_cast<i32>(node->as_i64_or(static_cast<i64>(out)));
    return true;
  }

  [[nodiscard]] bool read_u32(
      const io::DataValue& root,
      std::string_view field,
      u32& out,
      bool required = true) {
    const io::DataValue* node = member(root, field);
    if (node == nullptr) {
      return !required || fail(std::string{field}, "is required");
    }
    if (node->kind() != io::DataKind::I64 && node->kind() != io::DataKind::U64) {
      return fail(std::string{field}, "must be an integer");
    }
    out = static_cast<u32>(node->as_u64_or(out));
    return true;
  }

  [[nodiscard]] bool read_u64(
      const io::DataValue& root,
      std::string_view field,
      u64& out,
      bool required = true) {
    const io::DataValue* node = member(root, field);
    if (node == nullptr) {
      return !required || fail(std::string{field}, "is required");
    }
    if (node->kind() != io::DataKind::I64 && node->kind() != io::DataKind::U64) {
      return fail(std::string{field}, "must be an integer");
    }
    out = node->as_u64_or(out);
    return true;
  }

  [[nodiscard]] bool read_f32(
      const io::DataValue& root,
      std::string_view field,
      f32& out,
      bool required = true) {
    const io::DataValue* node = member(root, field);
    if (node == nullptr) {
      return !required || fail(std::string{field}, "is required");
    }
    if (node->kind() != io::DataKind::F64 &&
        node->kind() != io::DataKind::I64 &&
        node->kind() != io::DataKind::U64) {
      return fail(std::string{field}, "must be a number");
    }
    out = static_cast<f32>(node->as_f64_or(out));
    return true;
  }

  [[nodiscard]] bool read_bool(
      const io::DataValue& root,
      std::string_view field,
      bool& out,
      bool required = true) {
    const io::DataValue* node = member(root, field);
    if (node == nullptr) {
      return !required || fail(std::string{field}, "is required");
    }
    if (node->kind() != io::DataKind::Bool) {
      return fail(std::string{field}, "must be a boolean");
    }
    out = node->as_bool_or(out);
    return true;
  }

  [[nodiscard]] bool parse_float_range(
      const io::DataValue& node,
      std::string field,
      FloatRange& out) {
    const io::DataValue::Array* array = node.array_if();
    if (array == nullptr || array->size() != 2U) {
      return fail(std::move(field), "must be a two-number range");
    }
    out.min = static_cast<f32>((*array)[0].as_f64_or(0.0));
    out.max = static_cast<f32>((*array)[1].as_f64_or(0.0));
    return out.min <= out.max || fail(std::move(field), "range min must be <= max");
  }

  [[nodiscard]] bool read_int_range(
      const io::DataValue& root,
      std::string_view field,
      IntRange& out,
      std::string min_key = "min",
      std::string max_key = "max",
      bool required = true) {
    const io::DataValue* node = member(root, field);
    if (node == nullptr) {
      return !required || fail(std::string{field}, "is required");
    }
    if (!require_object(*node, field)) {
      return false;
    }
    if (!read_i32(*node, min_key, out.min) || !read_i32(*node, max_key, out.max)) {
      return false;
    }
    return out.min <= out.max || fail(std::string{field}, "range min must be <= max");
  }
};

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

[[nodiscard]] std::string derived_id_for(const asset::PackDataFile& file) {
  if (file.name_space.empty()) {
    return {};
  }
  const std::filesystem::path root = std::filesystem::path{"data"} / file.name_space;
  std::filesystem::path logical = file.relative_path.lexically_relative(root);
  logical.replace_extension();
  return file.name_space + ":" + logical.generic_string();
}

[[nodiscard]] bool read_common(
    Parser& parser,
    const io::DataValue& root,
    std::string_view expected_id,
    u32& out_version) {
  std::string actual_id;
  if (!parser.read_string(root, "id", actual_id)) {
    return false;
  }
  if (actual_id != expected_id) {
    return parser.fail(
        "id",
        "must match resource path identity '" + std::string{expected_id} + "'");
  }
  parser.id = std::string{expected_id};
  if (!parser.read_u32(root, "version", out_version)) {
    return false;
  }
  return out_version == SupportedSchemaVersion ||
         parser.fail("version", "unsupported worldgen schema version");
}

[[nodiscard]] bool parse_noise_field(
    Parser& parser,
    const io::DataValue& node,
    NoiseFieldConfig& out,
    std::string_view field_prefix) {
  if (!parser.require_object(node, field_prefix)) {
    return false;
  }
  if (!parser.read_string(node, "type", out.type_id)) {
    return false;
  }
  const auto parsed_type = noise_type_from_string(out.type_id);
  if (!parsed_type) {
    return parser.fail(std::string{field_prefix} + ".type", "unknown noise type: " + out.type_id);
  }
  out.type = parsed_type.value();
  if (!parser.read_string(node, "seed_salt", out.seed_salt) ||
      !parser.read_f32(node, "frequency", out.frequency) ||
      !parser.read_u32(node, "octaves", out.octaves) ||
      !parser.read_f32(node, "amplitude", out.amplitude) ||
      !parser.read_f32(node, "lacunarity", out.lacunarity) ||
      !parser.read_f32(node, "gain", out.gain)) {
    return false;
  }
  if (out.frequency <= 0.0F || out.octaves == 0U || out.amplitude <= 0.0F ||
      out.lacunarity <= 0.0F || out.gain <= 0.0F) {
    return parser.fail(std::string{field_prefix}, "noise numeric fields must be positive");
  }
  return true;
}

[[nodiscard]] bool parse_stage_feature_lists(
    Parser& parser,
    const io::DataValue& root,
    std::vector<StageFeatureList>& out,
    bool required = false) {
  const io::DataValue* features = parser.member(root, "features");
  if (features == nullptr) {
    return !required;
  }
  const io::DataValue* stages = parser.member(*features, "stages");
  if (stages == nullptr || stages->kind() != io::DataKind::Object) {
    return parser.fail("features.stages", "must be an object");
  }
  const io::DataValue::Object* stage_object = stages->object_if();
  for (const auto& [stage_name, entries] : *stage_object) {
    const auto stage = generation_stage_from_string(stage_name);
    if (!stage) {
      return parser.fail("features.stages." + stage_name, "unknown generation stage");
    }
    const io::DataValue::Array* array = entries.array_if();
    if (array == nullptr) {
      return parser.fail("features.stages." + stage_name, "must be a sequence");
    }
    StageFeatureList list;
    list.stage = stage.value();
    for (const io::DataValue& entry : *array) {
      if (entry.kind() != io::DataKind::String) {
        return parser.fail("features.stages." + stage_name, "feature references must be strings");
      }
      const std::string id{entry.as_string_or({})};
      if (!valid_resource_id(id)) {
        return parser.fail("features.stages." + stage_name, "feature reference must be a resource ID");
      }
      list.features.push_back({id});
    }
    out.push_back(std::move(list));
  }
  return true;
}

[[nodiscard]] bool parse_replace(
    Parser& parser,
    const io::DataValue& root,
    ReplaceRuleConfig& out,
    bool required = false) {
  const io::DataValue* replace = parser.member(root, "replace");
  if (replace == nullptr) {
    return !required;
  }
  out.specified = true;
  if (!parser.require_object(*replace, "replace")) {
    return false;
  }
  if (!parser.read_bool(*replace, "only_air", out.only_air, false)) {
    return false;
  }
  if (const io::DataValue* blocks = parser.member(*replace, "blocks"); blocks != nullptr) {
    const io::DataValue::Array* array = blocks->array_if();
    if (array == nullptr) {
      return parser.fail("replace.blocks", "must be a sequence");
    }
    for (const io::DataValue& entry : *array) {
      if (entry.kind() != io::DataKind::String) {
        return parser.fail("replace.blocks", "entries must be strings");
      }
      out.blocks.emplace_back(entry.as_string_or({}));
    }
  }
  return true;
}

[[nodiscard]] bool parse_block_list(
    Parser& parser,
    const io::DataValue& root,
    std::string_view field,
    std::vector<std::string>& out,
    bool required = false) {
  const io::DataValue* node = parser.member(root, field);
  if (node == nullptr) {
    return !required;
  }
  const io::DataValue::Array* array = node->array_if();
  if (array == nullptr) {
    return parser.fail(std::string{field}, "must be a sequence");
  }
  for (const io::DataValue& entry : *array) {
    if (entry.kind() != io::DataKind::String) {
      return parser.fail(std::string{field}, "entries must be strings");
    }
    out.emplace_back(entry.as_string_or({}));
  }
  return true;
}

[[nodiscard]] bool parse_filters(
    Parser& parser,
    const io::DataValue& root,
    std::vector<PlacementFilterConfig>& out,
    bool required = false) {
  const io::DataValue* filters = parser.member(root, "filters");
  if (filters == nullptr) {
    return !required;
  }
  const io::DataValue::Array* array = filters->array_if();
  if (array == nullptr) {
    return parser.fail("filters", "must be a sequence");
  }
  for (const io::DataValue& node : *array) {
    if (!parser.require_object(node, "filters[]")) {
      return false;
    }
    std::string type;
    if (!parser.read_string(node, "type", type)) {
      return false;
    }
    PlacementFilterConfig filter;
    if (type == "biome") {
      filter.type = PlacementFilterConfig::Type::Biome;
      if (!parser.read_ref(node, "biome", filter.biome)) {
        return false;
      }
    } else if (type == "biome_tag") {
      filter.type = PlacementFilterConfig::Type::BiomeTag;
      if (!parser.read_ref(node, "tag", filter.tag)) {
        return false;
      }
    } else if (type == "block_below") {
      filter.type = PlacementFilterConfig::Type::BlockBelow;
      if (!parse_block_list(parser, node, "blocks", filter.blocks, true)) {
        return false;
      }
    } else if (type == "replaceable") {
      filter.type = PlacementFilterConfig::Type::Replaceable;
    } else if (type == "slope_less_than") {
      filter.type = PlacementFilterConfig::Type::SlopeLessThan;
      if (!parser.read_f32(node, "value", filter.value)) {
        return false;
      }
    } else if (type == "slope_greater_than") {
      filter.type = PlacementFilterConfig::Type::SlopeGreaterThan;
      if (!parser.read_f32(node, "value", filter.value)) {
        return false;
      }
    } else if (type == "underwater") {
      filter.type = PlacementFilterConfig::Type::Underwater;
      if (!parser.read_bool(node, "value", filter.bool_value)) {
        return false;
      }
    } else {
      return parser.fail("filters[].type", "unknown placement filter type: " + type);
    }
    out.push_back(std::move(filter));
  }
  return true;
}

[[nodiscard]] bool parse_surface_predicate(
    Parser& parser,
    const io::DataValue& root,
    SurfaceRulePredicate& out) {
  if (!parser.read_bool(root, "above_sea_level", out.above_sea_level.emplace(), false)) {
    out.above_sea_level.reset();
    return false;
  }
  if (parser.member(root, "above_sea_level") == nullptr) {
    out.above_sea_level.reset();
  }
  if (!parser.read_bool(root, "below_sea_level", out.below_sea_level.emplace(), false)) {
    out.below_sea_level.reset();
    return false;
  }
  if (parser.member(root, "below_sea_level") == nullptr) {
    out.below_sea_level.reset();
  }
  if (!parser.read_bool(root, "underwater", out.underwater.emplace(), false)) {
    out.underwater.reset();
    return false;
  }
  if (parser.member(root, "underwater") == nullptr) {
    out.underwater.reset();
  }
  if (parser.member(root, "biome") != nullptr) {
    ResourceRef ref;
    if (!parser.read_ref(root, "biome", ref)) {
      return false;
    }
    out.biome = std::move(ref);
  }
  if (parser.member(root, "biome_tag") != nullptr) {
    ResourceRef ref;
    if (!parser.read_ref(root, "biome_tag", ref)) {
      return false;
    }
    out.biome_tag = std::move(ref);
  }
  if (parser.member(root, "biome_tag_not") != nullptr) {
    ResourceRef ref;
    if (!parser.read_ref(root, "biome_tag_not", ref)) {
      return false;
    }
    out.biome_tag_not = std::move(ref);
  }
  if (parser.member(root, "slope_less_than") != nullptr) {
    f32 value{};
    if (!parser.read_f32(root, "slope_less_than", value)) {
      return false;
    }
    out.slope_less_than = value;
  }
  if (parser.member(root, "slope_greater_than") != nullptr) {
    f32 value{};
    if (!parser.read_f32(root, "slope_greater_than", value)) {
      return false;
    }
    out.slope_greater_than = value;
  }
  if (parser.member(root, "height_above") != nullptr) {
    i32 value{};
    if (!parser.read_i32(root, "height_above", value)) {
      return false;
    }
    out.height_above = value;
  }
  if (parser.member(root, "height_below") != nullptr) {
    i32 value{};
    if (!parser.read_i32(root, "height_below", value)) {
      return false;
    }
    out.height_below = value;
  }
  const io::DataValue::Object* object = root.object_if();
  for (const auto& [key, value] : *object) {
    (void)value;
    if (key != "above_sea_level" && key != "below_sea_level" && key != "underwater" &&
        key != "biome" && key != "biome_tag" && key != "biome_tag_not" &&
        key != "slope_less_than" && key != "slope_greater_than" &&
        key != "height_above" && key != "height_below") {
      return parser.fail("rules[].when." + key, "unknown surface rule predicate");
    }
  }
  return true;
}

template<typename T>
[[nodiscard]] bool insert_unique(
    Parser& parser,
    HashMap<std::string, T>& map,
    std::string id,
    T value) {
  if (map.contains(id)) {
    return parser.fail("id", "duplicate worldgen resource ID");
  }
  map.emplace(std::move(id), std::move(value));
  return true;
}

template<typename Fn>
[[nodiscard]] RegistryResult load_category(
    const asset::PackManager& packs,
    std::string_view category,
    WorldGenRegistry registry,
    Fn&& parse_file) {
  for (const asset::PackDataFile& file : packs.data_files(category, ".yaml")) {
    const std::string id = derived_id_for(file);
    Parser parser;
    parser.id = id.empty() ? file.relative_path.generic_string() : id;
    if (id.empty()) {
      return RegistryResult::failure(
          {parser.id, "path", "worldgen resources must live under data/<namespace>/"});
    }
    const std::string text = read_text_file(file.path);
    auto decoded = io::decode_yaml_value(text);
    if (!decoded) {
      return RegistryResult::failure(
          {id, "root", "failed to parse YAML: " + decoded.error().describe()});
    }
    if (!parse_file(parser, decoded.value().root, id, registry)) {
      return RegistryResult::failure(parser.error);
    }
  }
  return RegistryResult::success(std::move(registry));
}

} // namespace

RegistryResult load_worldgen_registry_from_packs(const asset::PackManager& packs) {
  WorldGenRegistry registry;

  auto load_noise = load_category(
      packs,
      "worldgen/noise",
      std::move(registry),
      [](Parser& parser, const io::DataValue& root, std::string_view id, WorldGenRegistry& out) {
        NoiseFileConfig config;
        config.id = std::string{id};
        if (!read_common(parser, root, id, config.version)) {
          return false;
        }
        const io::DataValue* fields = parser.member(root, "fields");
        if (fields == nullptr || fields->kind() != io::DataKind::Object) {
          return parser.fail("fields", "must be an object");
        }
        const io::DataValue::Object* object = fields->object_if();
        for (const auto& [field_name, field_node] : *object) {
          NoiseFieldConfig field;
          if (!parse_noise_field(parser, field_node, field, "fields." + field_name)) {
            return false;
          }
          config.fields.emplace(field_name, std::move(field));
        }
        if (config.fields.empty()) {
          return parser.fail("fields", "must define at least one noise field");
        }
        return insert_unique(parser, out.noises, std::string{id}, std::move(config));
      });
  if (!load_noise) {
    return load_noise;
  }
  registry = std::move(load_noise.value());

  auto load_terrain = load_category(
      packs,
      "worldgen/terrain",
      std::move(registry),
      [](Parser& parser, const io::DataValue& root, std::string_view id, WorldGenRegistry& out) {
        TerrainConfig config;
        config.id = std::string{id};
        if (!read_common(parser, root, id, config.version) ||
            !parser.read_string(root, "type", config.type)) {
          return false;
        }
        if (config.type != "arvane:terrain/heightmap") {
          return parser.fail("type", "unknown terrain type: " + config.type);
        }
        const io::DataValue* height = parser.member(root, "height");
        if (height == nullptr || !parser.require_object(*height, "height")) {
          return false;
        }
        if (!parser.read_i32(*height, "base", config.height.base) ||
            !parser.read_i32(*height, "scale", config.height.scale) ||
            !parser.read_i32(*height, "hill_scale", config.height.hill_scale, false) ||
            !parser.read_i32(*height, "ridge_scale", config.height.ridge_scale, false) ||
            !parser.read_i32(*height, "detail_scale", config.height.detail_scale)) {
          return false;
        }
        if (const io::DataValue* shape = parser.member(root, "shape"); shape != nullptr) {
          if (!parser.require_object(*shape, "shape") ||
              !parser.read_f32(
                  *shape,
                  "ridge_sharpness",
                  config.shape.ridge_sharpness,
                  false) ||
              !parser.read_f32(
                  *shape,
                  "domain_warp_amplitude",
                  config.shape.domain_warp_amplitude,
                  false) ||
              !parser.read_f32(*shape, "terrace_step", config.shape.terrace_step, false) ||
              !parser.read_f32(
                  *shape,
                  "terrace_strength",
                  config.shape.terrace_strength,
                  false) ||
              !parser.read_i32(
                  *shape,
                  "max_column_step",
                  config.shape.max_column_step,
                  false)) {
            return false;
          }
          if (config.shape.ridge_sharpness <= 0.0F ||
              config.shape.domain_warp_amplitude < 0.0F ||
              config.shape.terrace_step < 0.0F ||
              config.shape.terrace_strength < 0.0F ||
              config.shape.terrace_strength > 1.0F ||
              config.shape.max_column_step < 0) {
            return parser.fail("shape", "numeric fields are outside supported ranges");
          }
        }
        const io::DataValue* noise = parser.member(root, "noise");
        if (noise == nullptr || !parser.require_object(*noise, "noise")) {
          return false;
        }
        const io::DataValue* primary = parser.member(*noise, "primary");
        const io::DataValue* detail = parser.member(*noise, "detail");
        if (primary == nullptr || detail == nullptr) {
          return parser.fail("noise", "primary and detail fields are required");
        }
        if (!parse_noise_field(parser, *primary, config.primary_noise, "noise.primary") ||
            !parse_noise_field(parser, *detail, config.detail_noise, "noise.detail")) {
          return false;
        }
        if (const io::DataValue* hill = parser.member(*noise, "hill"); hill != nullptr) {
          NoiseFieldConfig parsed;
          if (!parse_noise_field(parser, *hill, parsed, "noise.hill")) {
            return false;
          }
          config.hill_noise = std::move(parsed);
        }
        if (const io::DataValue* ridge = parser.member(*noise, "ridge"); ridge != nullptr) {
          NoiseFieldConfig parsed;
          if (!parse_noise_field(parser, *ridge, parsed, "noise.ridge")) {
            return false;
          }
          config.ridge_noise = std::move(parsed);
        }
        if (const io::DataValue* warp_x = parser.member(*noise, "warp_x"); warp_x != nullptr) {
          NoiseFieldConfig parsed;
          if (!parse_noise_field(parser, *warp_x, parsed, "noise.warp_x")) {
            return false;
          }
          config.warp_x_noise = std::move(parsed);
        }
        if (const io::DataValue* warp_z = parser.member(*noise, "warp_z"); warp_z != nullptr) {
          NoiseFieldConfig parsed;
          if (!parse_noise_field(parser, *warp_z, parsed, "noise.warp_z")) {
            return false;
          }
          config.warp_z_noise = std::move(parsed);
        }
        if (const io::DataValue* modifiers = parser.member(root, "biome_modifiers");
            modifiers != nullptr) {
          if (!parser.require_object(*modifiers, "biome_modifiers") ||
              !parser.read_bool(
                  *modifiers,
                  "base_height_offset",
                  config.biome_modifiers.base_height_offset,
                  false) ||
              !parser.read_bool(
                  *modifiers,
                  "height_scale_multiplier",
                  config.biome_modifiers.height_scale_multiplier,
                  false) ||
              !parser.read_bool(
                  *modifiers,
                  "hill_multiplier",
                  config.biome_modifiers.hill_multiplier,
                  false) ||
              !parser.read_bool(
                  *modifiers,
                  "ridge_multiplier",
                  config.biome_modifiers.ridge_multiplier,
                  false) ||
              !parser.read_bool(
                  *modifiers,
                  "roughness_multiplier",
                  config.biome_modifiers.roughness_multiplier,
                  false) ||
              !parser.read_bool(
                  *modifiers,
                  "terrace_step",
                  config.biome_modifiers.terrace_step,
                  false) ||
              !parser.read_bool(
                  *modifiers,
                  "terrace_strength",
                  config.biome_modifiers.terrace_strength,
                  false)) {
            return false;
          }
        }
        return insert_unique(parser, out.terrains, std::string{id}, std::move(config));
      });
  if (!load_terrain) {
    return load_terrain;
  }
  registry = std::move(load_terrain.value());

  auto load_surface = load_category(
      packs,
      "worldgen/surface_rule_set",
      std::move(registry),
      [](Parser& parser, const io::DataValue& root, std::string_view id, WorldGenRegistry& out) {
        SurfaceRuleSetConfig config;
        config.id = std::string{id};
        if (!read_common(parser, root, id, config.version)) {
          return false;
        }
        const io::DataValue* rules = parser.member(root, "rules");
        const io::DataValue::Array* rule_array = rules == nullptr ? nullptr : rules->array_if();
        if (rule_array == nullptr || rule_array->empty()) {
          return parser.fail("rules", "must be a non-empty sequence");
        }
        for (const io::DataValue& rule_node : *rule_array) {
          if (!parser.require_object(rule_node, "rules[]")) {
            return false;
          }
          SurfaceRuleConfig rule;
          if (const io::DataValue* when = parser.member(rule_node, "when"); when != nullptr) {
            if (!parser.require_object(*when, "rules[].when")) {
              return false;
            }
            SurfaceRulePredicate predicate;
            if (!parse_surface_predicate(parser, *when, predicate)) {
              return false;
            }
            rule.when = std::move(predicate);
          }
          const io::DataValue* layers = parser.member(rule_node, "layers");
          const io::DataValue::Array* layer_array =
              layers == nullptr ? nullptr : layers->array_if();
          if (layer_array == nullptr || layer_array->empty()) {
            return parser.fail("rules[].layers", "must be a non-empty sequence");
          }
          bool has_remaining = false;
          for (const io::DataValue& layer_node : *layer_array) {
            SurfaceLayerConfig layer;
            if (!parser.require_object(layer_node, "rules[].layers[]") ||
                !parser.read_string(layer_node, "block", layer.block)) {
              return false;
            }
            const io::DataValue* depth = parser.member(layer_node, "depth");
            if (depth == nullptr) {
              return parser.fail("rules[].layers[].depth", "is required");
            }
            if (depth->kind() == io::DataKind::String &&
                depth->as_string_or({}) == "remaining") {
              layer.remaining = true;
            } else {
              layer.depth = static_cast<i32>(depth->as_i64_or(0));
              if (layer.depth <= 0) {
                return parser.fail("rules[].layers[].depth", "must be positive or remaining");
              }
            }
            has_remaining = has_remaining || layer.remaining;
            rule.layers.push_back(std::move(layer));
          }
          if (!has_remaining) {
            return parser.fail("rules[].layers", "must include a remaining layer");
          }
          config.rules.push_back(std::move(rule));
        }
        return insert_unique(parser, out.surface_rule_sets, std::string{id}, std::move(config));
      });
  if (!load_surface) {
    return load_surface;
  }
  registry = std::move(load_surface.value());

  auto load_biome = load_category(
      packs,
      "worldgen/biome",
      std::move(registry),
      [](Parser& parser, const io::DataValue& root, std::string_view id, WorldGenRegistry& out) {
        BiomeConfig config;
        config.id = std::string{id};
        if (!read_common(parser, root, id, config.version)) {
          return false;
        }
        if (!parser.read_string(root, "display_name", config.display_name, false)) {
          return false;
        }
        if (config.display_name.empty()) {
          config.display_name = config.id;
        }
        if (const io::DataValue* tags = parser.member(root, "tags"); tags != nullptr) {
          const io::DataValue::Array* array = tags->array_if();
          if (array == nullptr) {
            return parser.fail("tags", "must be a sequence");
          }
          for (const io::DataValue& entry : *array) {
            const std::string tag{entry.as_string_or({})};
            if (!valid_resource_id(tag)) {
              return parser.fail("tags", "entries must be resource IDs");
            }
            config.tags.push_back({tag});
          }
        }
        const io::DataValue* terrain = parser.member(root, "terrain");
        if (terrain != nullptr) {
          if (!parser.require_object(*terrain, "terrain") ||
              !parser.read_f32(
                  *terrain,
                  "base_height_offset",
                  config.terrain.base_height_offset,
                  false) ||
              !parser.read_f32(
                  *terrain,
                  "height_scale_multiplier",
                  config.terrain.height_scale_multiplier,
                  false) ||
              !parser.read_f32(
                  *terrain,
                  "hill_multiplier",
                  config.terrain.hill_multiplier,
                  false) ||
              !parser.read_f32(
                  *terrain,
                  "ridge_multiplier",
                  config.terrain.ridge_multiplier,
                  false) ||
              !parser.read_f32(
                  *terrain,
                  "roughness_multiplier",
                  config.terrain.roughness_multiplier,
                  false) ||
              !parser.read_f32(
                  *terrain,
                  "terrace_step",
                  config.terrain.terrace_step,
                  false) ||
              !parser.read_f32(
                  *terrain,
                  "terrace_strength",
                  config.terrain.terrace_strength,
                  false)) {
            return false;
          }
          if (config.terrain.height_scale_multiplier < 0.0F ||
              config.terrain.hill_multiplier < 0.0F ||
              config.terrain.ridge_multiplier < 0.0F ||
              config.terrain.roughness_multiplier < 0.0F ||
              config.terrain.terrace_step < 0.0F ||
              config.terrain.terrace_strength < 0.0F ||
              config.terrain.terrace_strength > 1.0F) {
            return parser.fail("terrain", "numeric terrain modifiers are outside supported ranges");
          }
        }
        if (!parser.read_ref(root, "surface_rule_set", config.surface_rule_set, false) ||
            !parse_stage_feature_lists(parser, root, config.features, false)) {
          return false;
        }
        if (const io::DataValue* colors = parser.member(root, "colors"); colors != nullptr) {
          if (!parser.require_object(*colors, "colors")) {
            return false;
          }
          auto read_color = [&](std::string_view field, std::optional<ColorValue>& color) {
            if (parser.member(*colors, field) == nullptr) {
              return true;
            }
            std::string text;
            if (!parser.read_string(*colors, field, text)) {
              return false;
            }
            if (!valid_color(text)) {
              return parser.fail("colors." + std::string{field}, "must be #RRGGBB");
            }
            color = ColorValue{std::move(text)};
            return true;
          };
          if (!read_color("grass", config.colors.grass) ||
              !read_color("foliage", config.colors.foliage) ||
              !read_color("water", config.colors.water) ||
              !read_color("fog", config.colors.fog)) {
            return false;
          }
        }
        return insert_unique(parser, out.biomes, std::string{id}, std::move(config));
      });
  if (!load_biome) {
    return load_biome;
  }
  registry = std::move(load_biome.value());

  auto load_biome_source = load_category(
      packs,
      "worldgen/biome_source",
      std::move(registry),
      [](Parser& parser, const io::DataValue& root, std::string_view id, WorldGenRegistry& out) {
        BiomeSourceConfig config;
        config.id = std::string{id};
        if (!read_common(parser, root, id, config.version) ||
            !parser.read_string(root, "type", config.type) ||
            !parser.read_ref(root, "noise", config.noise)) {
          return false;
        }
        if (config.type != "arvane:biome_source/climate_map") {
          return parser.fail("type", "unknown biome source type: " + config.type);
        }
        if (const io::DataValue* sample_scale = parser.member(root, "sample_scale");
            sample_scale != nullptr) {
          if (!parser.require_object(*sample_scale, "sample_scale") ||
              !parser.read_i32(*sample_scale, "xz", config.sample_scale_xz)) {
            return false;
          }
        }
        if (config.sample_scale_xz <= 0) {
          return parser.fail("sample_scale.xz", "must be positive");
        }
        const io::DataValue* biomes = parser.member(root, "biomes");
        const io::DataValue::Array* biome_array =
            biomes == nullptr ? nullptr : biomes->array_if();
        if (biome_array == nullptr || biome_array->empty()) {
          return parser.fail("biomes", "must be a non-empty sequence");
        }
        for (const io::DataValue& biome_node : *biome_array) {
          BiomeSourceEntryConfig entry;
          if (!parser.require_object(biome_node, "biomes[]") ||
              !parser.read_ref(biome_node, "biome", entry.biome)) {
            return false;
          }
          const io::DataValue* climate = parser.member(biome_node, "climate");
          if (climate == nullptr || climate->kind() != io::DataKind::Object) {
            return parser.fail("biomes[].climate", "must be an object");
          }
          for (const auto& [field_name, range_node] : *climate->object_if()) {
            ClimateRange range;
            range.field = field_name;
            if (!parser.parse_float_range(range_node, "biomes[].climate." + field_name, range.range)) {
              return false;
            }
            entry.climate.push_back(std::move(range));
          }
          config.biomes.push_back(std::move(entry));
        }
        return insert_unique(parser, out.biome_sources, std::string{id}, std::move(config));
      });
  if (!load_biome_source) {
    return load_biome_source;
  }
  registry = std::move(load_biome_source.value());

  auto load_carver = load_category(
      packs,
      "worldgen/carver",
      std::move(registry),
      [](Parser& parser, const io::DataValue& root, std::string_view id, WorldGenRegistry& out) {
        CarverConfig config;
        config.id = std::string{id};
        if (!read_common(parser, root, id, config.version) ||
            !parser.read_string(root, "type", config.type)) {
          return false;
        }
        if (config.type == "arvane:carver/noise_caves") {
          config.carver_type = CarverType::NoiseCaves;
        } else if (config.type == "arvane:carver/ravines") {
          config.carver_type = CarverType::Ravines;
        } else if (config.type == "arvane:carver/tunnel_caves") {
          config.carver_type = CarverType::TunnelCaves;
        } else {
          return parser.fail("type", "unknown carver type: " + config.type);
        }
        if (!parser.read_bool(root, "enabled", config.enabled, false)) {
          return false;
        }
        if (!parser.read_int_range(root, "y_range", config.y_range) ||
            !parser.read_i32(root, "surface_margin", config.surface_margin) ||
            !parser.read_i32(root, "floor_margin", config.floor_margin, false)) {
          return false;
        }
        if (config.surface_margin < 0 || config.floor_margin < 0) {
          return parser.fail("root", "surface_margin and floor_margin must be non-negative");
        }
        if (config.carver_type == CarverType::NoiseCaves) {
          const io::DataValue* noise = parser.member(root, "noise");
          if (noise == nullptr ||
              !parse_noise_field(parser, *noise, config.noise, "noise") ||
              !parser.read_f32(root, "threshold", config.threshold)) {
            return false;
          }
          if (config.threshold < -1.0F || config.threshold > 1.0F) {
            return parser.fail("threshold", "must be between -1.0 and 1.0");
          }
        } else if (config.carver_type == CarverType::Ravines) {
          if (!parser.read_u64(root, "salt", config.ravine.salt) ||
              !parser.read_i32(root, "region_size_chunks", config.ravine.region_size_chunks) ||
              !parser.read_f32(root, "chance_per_region", config.ravine.chance_per_region) ||
              !parser.read_int_range(root, "length_chunks", config.ravine.length_chunks) ||
              !parser.read_int_range(root, "depth", config.ravine.depth)) {
            return false;
          }
          const io::DataValue* width = parser.member(root, "width");
          if (width == nullptr ||
              !parser.parse_float_range(*width, "width", config.ravine.width)) {
            return false;
          }
          if (config.ravine.region_size_chunks <= 0 ||
              config.ravine.chance_per_region < 0.0F ||
              config.ravine.chance_per_region > 1.0F ||
              config.ravine.length_chunks.min <= 0 ||
              config.ravine.width.min <= 0.0F ||
              config.ravine.depth.min <= 0) {
            return parser.fail("root", "ravine numeric fields are outside supported ranges");
          }
        } else {
          if (!parser.read_u64(root, "salt", config.tunnel.salt) ||
              !parser.read_i32(root, "region_size_chunks", config.tunnel.region_size_chunks) ||
              !parser.read_f32(root, "chance_per_region", config.tunnel.chance_per_region) ||
              !parser.read_int_range(root, "tunnels_per_region", config.tunnel.tunnels_per_region) ||
              !parser.read_int_range(root, "length_blocks", config.tunnel.length_blocks) ||
              !parser.read_int_range(root, "start_y", config.tunnel.start_y) ||
              !parser.read_f32(root, "horizontal_curl", config.tunnel.horizontal_curl) ||
              !parser.read_f32(root, "vertical_curl", config.tunnel.vertical_curl) ||
              !parser.read_f32(root, "branch_chance", config.tunnel.branch_chance) ||
              !parser.read_int_range(root, "branch_count", config.tunnel.branch_count) ||
              !parser.read_f32(root, "room_chance", config.tunnel.room_chance)) {
            return false;
          }
          const io::DataValue* radius = parser.member(root, "radius");
          const io::DataValue* vertical_radius_multiplier =
              parser.member(root, "vertical_radius_multiplier");
          const io::DataValue* room_radius = parser.member(root, "room_radius");
          if (radius == nullptr ||
              !parser.parse_float_range(*radius, "radius", config.tunnel.radius) ||
              vertical_radius_multiplier == nullptr ||
              !parser.parse_float_range(
                  *vertical_radius_multiplier,
                  "vertical_radius_multiplier",
                  config.tunnel.vertical_radius_multiplier) ||
              room_radius == nullptr ||
              !parser.parse_float_range(*room_radius, "room_radius", config.tunnel.room_radius)) {
            return false;
          }
          if (config.tunnel.region_size_chunks <= 0 ||
              config.tunnel.chance_per_region < 0.0F ||
              config.tunnel.chance_per_region > 1.0F ||
              config.tunnel.tunnels_per_region.min <= 0 ||
              config.tunnel.length_blocks.min <= 0 ||
              config.tunnel.radius.min <= 0.0F ||
              config.tunnel.vertical_radius_multiplier.min <= 0.0F ||
              config.tunnel.horizontal_curl < 0.0F ||
              config.tunnel.vertical_curl < 0.0F ||
              config.tunnel.branch_chance < 0.0F ||
              config.tunnel.branch_chance > 1.0F ||
              config.tunnel.branch_count.min < 0 ||
              config.tunnel.room_chance < 0.0F ||
              config.tunnel.room_chance > 1.0F ||
              config.tunnel.room_radius.min <= 0.0F) {
            return parser.fail("root", "tunnel cave numeric fields are outside supported ranges");
          }
        }
        return insert_unique(parser, out.carvers, std::string{id}, std::move(config));
      });
  if (!load_carver) {
    return load_carver;
  }
  registry = std::move(load_carver.value());

  auto load_configured_feature = load_category(
      packs,
      "worldgen/configured_feature",
      std::move(registry),
      [](Parser& parser, const io::DataValue& root, std::string_view id, WorldGenRegistry& out) {
        ConfiguredFeatureConfig config;
        config.id = std::string{id};
        if (!read_common(parser, root, id, config.version) ||
            !parser.read_string(root, "type", config.type)) {
          return false;
        }
        if (config.type == "arvane:feature/simple_tree") {
          SimpleTreeFeatureConfig tree;
          const io::DataValue* trunk = parser.member(root, "trunk");
          const io::DataValue* leaves = parser.member(root, "leaves");
          if (trunk == nullptr || leaves == nullptr ||
              !parser.require_object(*trunk, "trunk") ||
              !parser.require_object(*leaves, "leaves") ||
              !parser.read_string(*trunk, "block", tree.trunk_block) ||
              !parser.read_int_range(*trunk, "height", tree.trunk_height) ||
              !parser.read_string(*leaves, "block", tree.leaves_block) ||
              !parser.read_int_range(*leaves, "radius", tree.leaves_radius)) {
            return false;
          }
          config.config = std::move(tree);
        } else if (config.type == "arvane:feature/patch") {
          PatchFeatureConfig patch;
          const io::DataValue* radius = parser.member(root, "radius");
          if (radius == nullptr ||
              !parser.read_string(root, "block", patch.block) ||
              !parser.read_i32(root, "tries", patch.tries) ||
              !parser.require_object(*radius, "radius") ||
              !parser.read_i32(*radius, "xz", patch.radius_xz) ||
              !parser.read_i32(*radius, "y", patch.radius_y) ||
              !parse_replace(parser, root, patch.replace, true) ||
              !parse_block_list(parser, root, "place_on", patch.place_on.blocks, false)) {
            return false;
          }
          config.config = std::move(patch);
        } else if (config.type == "arvane:feature/disk") {
          DiskFeatureConfig disk;
          if (!parser.read_string(root, "block", disk.block) ||
              !parser.read_int_range(root, "radius", disk.radius) ||
              !parser.read_i32(root, "depth", disk.depth) ||
              !parse_replace(parser, root, disk.replace, true)) {
            return false;
          }
          config.config = std::move(disk);
        } else if (config.type == "arvane:feature/ore_vein") {
          OreVeinFeatureConfig ore;
          if (!parser.read_string(root, "block", ore.block) ||
              !parse_replace(parser, root, ore.replace, true) ||
              !parser.read_int_range(root, "vein_size", ore.vein_size)) {
            return false;
          }
          config.config = std::move(ore);
        } else if (config.type == "arvane:feature/single_block") {
          SingleBlockFeatureConfig block;
          if (!parser.read_string(root, "block", block.block) ||
              !parse_replace(parser, root, block.replace, true) ||
              !parse_block_list(parser, root, "place_on", block.place_on.blocks, false)) {
            return false;
          }
          config.config = std::move(block);
        } else if (config.type == "arvane:feature/blueprint") {
          BlueprintFeatureConfig blueprint;
          if (!parser.read_ref(root, "blueprint", blueprint.blueprint)) {
            return false;
          }
          if (const io::DataValue* rotation = parser.member(root, "rotation");
              rotation != nullptr) {
            if (!parser.require_object(*rotation, "rotation") ||
                !parser.read_bool(*rotation, "random_y", blueprint.random_y_rotation, false)) {
              return false;
            }
          }
          if (!parser.read_f32(root, "integrity", blueprint.integrity, false)) {
            return false;
          }
          if (!parse_replace(parser, root, blueprint.replace, false)) {
            return false;
          }
          config.config = std::move(blueprint);
        } else {
          return parser.fail("type", "unknown configured feature type: " + config.type);
        }
        return insert_unique(parser, out.configured_features, std::string{id}, std::move(config));
      });
  if (!load_configured_feature) {
    return load_configured_feature;
  }
  registry = std::move(load_configured_feature.value());

  auto load_placed_feature = load_category(
      packs,
      "worldgen/placed_feature",
      std::move(registry),
      [](Parser& parser, const io::DataValue& root, std::string_view id, WorldGenRegistry& out) {
        PlacedFeatureConfig config;
        config.id = std::string{id};
        if (!read_common(parser, root, id, config.version) ||
            !parser.read_ref(root, "feature", config.feature) ||
            !parser.read_u64(root, "salt", config.salt)) {
          return false;
        }
        const io::DataValue* placement = parser.member(root, "placement");
        if (placement == nullptr || !parser.require_object(*placement, "placement")) {
          return false;
        }
        std::string stage;
        if (!parser.read_string(*placement, "stage", stage)) {
          return false;
        }
        const auto parsed_stage = generation_stage_from_string(stage);
        if (!parsed_stage) {
          return parser.fail("placement.stage", "unknown generation stage");
        }
        config.stage = parsed_stage.value();
        const io::DataValue* count = parser.member(*placement, "count");
        if (count == nullptr || !parser.require_object(*count, "placement.count")) {
          return false;
        }
        std::string count_type;
        if (!parser.read_string(*count, "type", count_type)) {
          return false;
        }
        if (count_type == "chance_per_chunk") {
          config.count.type = PlacementCountConfig::Type::ChancePerChunk;
          if (!parser.read_f32(*count, "chance", config.count.chance)) {
            return false;
          }
        } else if (count_type == "uniform") {
          config.count.type = PlacementCountConfig::Type::Uniform;
          if (!parser.read_i32(*count, "min", config.count.min) ||
              !parser.read_i32(*count, "max", config.count.max)) {
            return false;
          }
        } else {
          return parser.fail("placement.count.type", "unknown count type");
        }
        const io::DataValue* height = parser.member(*placement, "height");
        if (height == nullptr || !parser.require_object(*height, "placement.height")) {
          return false;
        }
        std::string height_type;
        if (!parser.read_string(*height, "type", height_type)) {
          return false;
        }
        if (height_type == "surface") {
          config.height.type = PlacementHeightConfig::Type::Surface;
        } else if (height_type == "uniform") {
          config.height.type = PlacementHeightConfig::Type::Uniform;
          if (!parser.read_i32(*height, "min_y", config.height.min_y) ||
              !parser.read_i32(*height, "max_y", config.height.max_y)) {
            return false;
          }
        } else if (height_type == "trapezoid") {
          config.height.type = PlacementHeightConfig::Type::Trapezoid;
          if (!parser.read_i32(*height, "min_y", config.height.min_y) ||
              !parser.read_i32(*height, "max_y", config.height.max_y) ||
              !parser.read_i32(*height, "peak_y", config.height.peak_y)) {
            return false;
          }
        } else {
          return parser.fail("placement.height.type", "unknown height type");
        }
        if (!parse_filters(parser, *placement, config.filters, false)) {
          return false;
        }
        return insert_unique(parser, out.placed_features, std::string{id}, std::move(config));
      });
  if (!load_placed_feature) {
    return load_placed_feature;
  }
  registry = std::move(load_placed_feature.value());

  auto load_structure_set = load_category(
      packs,
      "worldgen/structure_set",
      std::move(registry),
      [](Parser& parser, const io::DataValue& root, std::string_view id, WorldGenRegistry& out) {
        StructureSetConfig config;
        config.id = std::string{id};
        if (!read_common(parser, root, id, config.version) ||
            !parser.read_u64(root, "salt", config.salt)) {
          return false;
        }
        const io::DataValue* structures = parser.member(root, "structures");
        const io::DataValue::Array* structure_array =
            structures == nullptr ? nullptr : structures->array_if();
        if (structure_array == nullptr || structure_array->empty()) {
          return parser.fail("structures", "must be a non-empty sequence");
        }
        for (const io::DataValue& node : *structure_array) {
          StructureEntryConfig entry;
          if (!parser.require_object(node, "structures[]") ||
              !parser.read_ref(node, "feature", entry.feature) ||
              !parser.read_i32(node, "weight", entry.weight)) {
            return false;
          }
          config.structures.push_back(std::move(entry));
        }
        const io::DataValue* placement = parser.member(root, "placement");
        if (placement == nullptr || !parser.require_object(*placement, "placement") ||
            !parser.read_string(*placement, "type", config.placement.type) ||
            !parser.read_i32(
                *placement,
                "average_distance_chunks",
                config.placement.average_distance_chunks) ||
            !parser.read_i32(
                *placement,
                "separation_chunks",
                config.placement.separation_chunks)) {
          return false;
        }
        if (config.placement.type != "grid") {
          return parser.fail("placement.type", "unknown structure placement type");
        }
        if (!parse_filters(parser, root, config.filters, false)) {
          return false;
        }
        return insert_unique(parser, out.structure_sets, std::string{id}, std::move(config));
      });
  if (!load_structure_set) {
    return load_structure_set;
  }
  registry = std::move(load_structure_set.value());

  auto load_dimension = load_category(
      packs,
      "worldgen/dimension",
      std::move(registry),
      [](Parser& parser, const io::DataValue& root, std::string_view id, WorldGenRegistry& out) {
        DimensionConfig config;
        config.id = std::string{id};
        if (!read_common(parser, root, id, config.version) ||
            !parser.read_string(root, "seed_salt", config.seed_salt)) {
          return false;
        }
        const io::DataValue* vertical = parser.member(root, "vertical");
        if (vertical == nullptr || !parser.require_object(*vertical, "vertical") ||
            !parser.read_i32(*vertical, "min_y", config.vertical.min_y) ||
            !parser.read_i32(*vertical, "max_y", config.vertical.max_y) ||
            !parser.read_i32(*vertical, "sea_level", config.vertical.sea_level)) {
          return false;
        }
        const io::DataValue* generator = parser.member(root, "generator");
        if (generator == nullptr || !parser.require_object(*generator, "generator") ||
            !parser.read_string(*generator, "type", config.generator.type)) {
          return false;
        }
        if (config.generator.type != "arvane:generator/layered_heightmap") {
          return parser.fail("generator.type", "unknown generator type");
        }
        if (const io::DataValue* climate = parser.member(root, "climate"); climate != nullptr) {
          if (!parser.require_object(*climate, "climate")) {
            return false;
          }
          const io::DataValue* fields = parser.member(*climate, "fields");
          if (fields == nullptr || fields->kind() != io::DataKind::Object) {
            return parser.fail("climate.fields", "must be an object");
          }
          for (const auto& [field_name, field_node] : *fields->object_if()) {
            ClimateFieldAdjustmentConfig adjustment;
            adjustment.field = field_name;
            if (adjustment.field.empty() ||
                !parser.require_object(field_node, "climate.fields." + field_name) ||
                !parser.read_f32(field_node, "scale", adjustment.scale, false) ||
                !parser.read_f32(field_node, "offset", adjustment.offset, false)) {
              return false;
            }
            if (adjustment.scale < 0.0F || adjustment.scale > 4.0F ||
                adjustment.offset < -2.0F || adjustment.offset > 2.0F) {
              return parser.fail(
                  "climate.fields." + field_name,
                  "scale must be 0..4 and offset must be -2..2");
            }
            config.climate_field_adjustments.push_back(std::move(adjustment));
          }
        }
        if (!parser.read_ref(root, "biome_source", config.biome_source) ||
            !parser.read_ref(root, "terrain", config.terrain) ||
            !parser.read_ref(root, "surface_rule_set", config.surface_rule_set, false)) {
          return false;
        }
        if (const io::DataValue* carvers = parser.member(root, "carvers"); carvers != nullptr) {
          const io::DataValue::Array* array = carvers->array_if();
          if (array == nullptr) {
            return parser.fail("carvers", "must be a sequence");
          }
          for (const io::DataValue& entry : *array) {
            const std::string ref{entry.as_string_or({})};
            if (!valid_resource_id(ref)) {
              return parser.fail("carvers", "entries must be resource IDs");
            }
            config.carvers.push_back({ref});
          }
        }
        if (!parse_stage_feature_lists(parser, root, config.features, false)) {
          return false;
        }
        if (const io::DataValue* structure_sets = parser.member(root, "structure_sets");
            structure_sets != nullptr) {
          const io::DataValue::Array* array = structure_sets->array_if();
          if (array == nullptr) {
            return parser.fail("structure_sets", "must be a sequence");
          }
          for (const io::DataValue& entry : *array) {
            const std::string ref{entry.as_string_or({})};
            if (!valid_resource_id(ref)) {
              return parser.fail("structure_sets", "entries must be resource IDs");
            }
            config.structure_sets.push_back({ref});
          }
        }
        return insert_unique(parser, out.dimensions, std::string{id}, std::move(config));
      });
  if (!load_dimension) {
    return load_dimension;
  }
  registry = std::move(load_dimension.value());

  for (const asset::PackDataFile& file : packs.data_files("blueprint", ".abp")) {
    const std::string id = derived_id_for(file);
    if (id.empty()) {
      return RegistryResult::failure(
          {file.relative_path.generic_string(), "path", "blueprints must live under data/<namespace>/"});
    }
    const std::string text = read_text_file(file.path);
    const blueprint::BlueprintCodecResult decoded = blueprint::decode_blueprint(text);
    if (!decoded.ok) {
      return RegistryResult::failure({id, "root", decoded.message});
    }
    registry.blueprints.emplace(id, decoded.blueprint);
  }

  return RegistryResult::success(std::move(registry));
}

} // namespace arv::world::worldgen
