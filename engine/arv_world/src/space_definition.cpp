#include "arv/world/space_definition.hpp"

#include "arv/io/data_codecs.hpp"
#include "arv/world/worldgen/common.hpp"

#include <fstream>
#include <sstream>
#include <utility>

namespace arv::world {
namespace {

struct Parser {
  std::string id{};
  SpaceDefinitionDiagnostic error{};

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
      std::string field_path) {
    return value.kind() == io::DataKind::Object ||
           fail(std::move(field_path), "must be an object");
  }

  [[nodiscard]] bool read_string(
      const io::DataValue& root,
      std::string_view field,
      std::string& out,
      bool required = true,
      std::string field_path = {}) {
    const io::DataValue* node = member(root, field);
    if (field_path.empty()) {
      field_path = std::string{field};
    }
    if (node == nullptr) {
      return !required || fail(std::move(field_path), "is required");
    }
    if (node->kind() != io::DataKind::String) {
      return fail(std::move(field_path), "must be a string");
    }
    out = std::string{node->as_string_or({})};
    return true;
  }

  [[nodiscard]] bool read_ref(
      const io::DataValue& root,
      std::string_view field,
      std::string& out,
      bool required = true,
      std::string field_path = {}) {
    if (!read_string(root, field, out, required, field_path)) {
      return false;
    }
    if (out.empty()) {
      return !required;
    }
    if (field_path.empty()) {
      field_path = std::string{field};
    }
    return worldgen::valid_resource_id(out) ||
           fail(std::move(field_path), "must be a valid resource ID");
  }

  [[nodiscard]] bool read_bool(
      const io::DataValue& root,
      std::string_view field,
      bool& out,
      bool required = true,
      std::string field_path = {}) {
    const io::DataValue* node = member(root, field);
    if (field_path.empty()) {
      field_path = std::string{field};
    }
    if (node == nullptr) {
      return !required || fail(std::move(field_path), "is required");
    }
    if (node->kind() != io::DataKind::Bool) {
      return fail(std::move(field_path), "must be a boolean");
    }
    out = node->as_bool_or(out);
    return true;
  }

  [[nodiscard]] bool read_i32(
      const io::DataValue& root,
      std::string_view field,
      i32& out,
      bool required = true,
      std::string field_path = {}) {
    const io::DataValue* node = member(root, field);
    if (field_path.empty()) {
      field_path = std::string{field};
    }
    if (node == nullptr) {
      return !required || fail(std::move(field_path), "is required");
    }
    if (node->kind() != io::DataKind::I64 && node->kind() != io::DataKind::U64) {
      return fail(std::move(field_path), "must be an integer");
    }
    out = static_cast<i32>(node->as_i64_or(static_cast<i64>(out)));
    return true;
  }

  [[nodiscard]] bool read_u32(
      const io::DataValue& root,
      std::string_view field,
      u32& out,
      bool required = true,
      std::string field_path = {}) {
    const io::DataValue* node = member(root, field);
    if (field_path.empty()) {
      field_path = std::string{field};
    }
    if (node == nullptr) {
      return !required || fail(std::move(field_path), "is required");
    }
    if (node->kind() != io::DataKind::I64 && node->kind() != io::DataKind::U64) {
      return fail(std::move(field_path), "must be an integer");
    }
    out = static_cast<u32>(node->as_u64_or(out));
    return true;
  }

  [[nodiscard]] bool read_f32(
      const io::DataValue& root,
      std::string_view field,
      f32& out,
      bool required = true,
      std::string field_path = {}) {
    const io::DataValue* node = member(root, field);
    if (field_path.empty()) {
      field_path = std::string{field};
    }
    if (node == nullptr) {
      return !required || fail(std::move(field_path), "is required");
    }
    if (!io::is_number_value(*node)) {
      return fail(std::move(field_path), "must be a number");
    }
    out = static_cast<f32>(node->as_f64_or(out));
    return true;
  }

  [[nodiscard]] bool read_vec3(
      const io::DataValue& root,
      std::string_view field,
      Vec3f& out,
      bool required = true,
      std::string field_path = {}) {
    const io::DataValue* node = member(root, field);
    if (field_path.empty()) {
      field_path = std::string{field};
    }
    if (node == nullptr) {
      return !required || fail(std::move(field_path), "is required");
    }
    const io::DataValue::Array* values = node->array_if();
    if (values == nullptr || values->size() != 3U) {
      return fail(std::move(field_path), "must be a three-number vector");
    }
    for (const io::DataValue& value : *values) {
      if (!io::is_number_value(value)) {
        return fail(std::move(field_path), "must be a three-number vector");
      }
    }
    out = {
        static_cast<f32>((*values)[0].as_f64_or(0.0)),
        static_cast<f32>((*values)[1].as_f64_or(0.0)),
        static_cast<f32>((*values)[2].as_f64_or(0.0))};
    return finite(out) || fail(std::move(field_path), "must contain finite numbers");
  }

  [[nodiscard]] bool read_ref_list(
      const io::DataValue& root,
      std::string_view field,
      std::vector<std::string>& out,
      bool required = true,
      std::string field_path = {}) {
    const io::DataValue* node = member(root, field);
    if (field_path.empty()) {
      field_path = std::string{field};
    }
    if (node == nullptr) {
      return !required || fail(std::move(field_path), "is required");
    }
    const io::DataValue::Array* values = node->array_if();
    if (values == nullptr) {
      return fail(std::move(field_path), "must be a sequence");
    }
    out.clear();
    out.reserve(values->size());
    for (usize index = 0; index < values->size(); ++index) {
      const io::DataValue& entry = (*values)[index];
      const std::string item_path = field_path + "[" + std::to_string(index) + "]";
      if (entry.kind() != io::DataKind::String) {
        return fail(item_path, "must be a string");
      }
      std::string value{entry.as_string_or({})};
      if (!worldgen::valid_resource_id(value)) {
        return fail(item_path, "must be a valid resource ID");
      }
      out.push_back(std::move(value));
    }
    return true;
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
  parser.id = std::string{expected_id};
  if (root.kind() != io::DataKind::Object) {
    return parser.fail("root", "must be an object");
  }
  std::string actual_id;
  if (!parser.read_string(root, "id", actual_id)) {
    return false;
  }
  if (actual_id != expected_id) {
    return parser.fail(
        "id",
        "must match resource path identity '" + std::string{expected_id} + "'");
  }
  if (!parser.read_u32(root, "version", out_version)) {
    return false;
  }
  return out_version == SpaceDefinitionSchemaVersion ||
         parser.fail("version", "unsupported space definition schema version");
}

template<typename T>
[[nodiscard]] bool insert_unique(
    Parser& parser,
    HashMap<std::string, T>& map,
    std::string id,
    T value) {
  if (map.contains(id)) {
    return parser.fail("id", "duplicate space definition resource ID");
  }
  map.emplace(std::move(id), std::move(value));
  return true;
}

[[nodiscard]] bool read_world_kind(
    Parser& parser,
    const io::DataValue& root,
    WorldDefinitionKind& out) {
  std::string value;
  if (!parser.read_string(root, "kind", value)) {
    return false;
  }
  const std::optional<WorldDefinitionKind> parsed =
      world_definition_kind_from_string(value);
  if (!parsed.has_value()) {
    return parser.fail("kind", "must be one of: planet, space");
  }
  out = parsed.value();
  return true;
}

[[nodiscard]] bool read_body_type(
    Parser& parser,
    const io::DataValue& root,
    SpaceBodyType& out) {
  std::string value;
  if (!parser.read_string(root, "type", value)) {
    return false;
  }
  const std::optional<SpaceBodyType> parsed = space_body_type_from_string(value);
  if (!parsed.has_value()) {
    return parser.fail(
        "type",
        "must be one of: planet, moon, station, asteroid_field");
  }
  out = parsed.value();
  return true;
}

[[nodiscard]] bool read_entry_mode(
    Parser& parser,
    const io::DataValue& root,
    SpaceBodyEntryMode& out,
    std::string field_path) {
  std::string value;
  if (!parser.read_string(root, "mode", value, true, field_path)) {
    return false;
  }
  const std::optional<SpaceBodyEntryMode> parsed =
      space_body_entry_mode_from_string(value);
  if (!parsed.has_value()) {
    return parser.fail(
        std::move(field_path),
        "must be one of: fixed_spawn, directional_spawn, docking_spawn, disabled");
  }
  out = parsed.value();
  return true;
}

[[nodiscard]] bool parse_rules(
    Parser& parser,
    const io::DataValue& root,
    WorldRulesDefinition& out) {
  const io::DataValue* node = parser.member(root, "rules");
  if (node == nullptr) {
    return true;
  }
  if (!parser.require_object(*node, "rules")) {
    return false;
  }
  return parser.read_bool(
             *node,
             "allow_block_placement",
             out.allow_block_placement,
             false,
             "rules.allow_block_placement") &&
         parser.read_bool(
             *node,
             "allow_vehicle_launch",
             out.allow_vehicle_launch,
             false,
             "rules.allow_vehicle_launch") &&
         parser.read_bool(
             *node,
             "allow_space_entry",
             out.allow_space_entry,
             false,
             "rules.allow_space_entry") &&
         parser.read_bool(
             *node,
             "unload_empty_chunks_aggressively",
             out.unload_empty_chunks_aggressively,
             false,
             "rules.unload_empty_chunks_aggressively");
}

[[nodiscard]] bool parse_transition_spawn(
    Parser& parser,
    const io::DataValue& node,
    SpaceTransitionSpawnDefinition& out,
    std::string_view prefix) {
  return parser.read_string(
             node,
             "mode",
             out.mode,
             false,
             std::string{prefix} + ".mode") &&
         parser.read_vec3(
             node,
             "position",
             out.position,
             false,
             std::string{prefix} + ".position") &&
         parser.read_vec3(
             node,
             "velocity",
             out.velocity,
             false,
             std::string{prefix} + ".velocity") &&
         parser.read_vec3(
             node,
             "rotation",
             out.rotation_degrees,
             false,
             std::string{prefix} + ".rotation") &&
         parser.read_f32(
             node,
             "velocity_scale",
             out.velocity_scale,
             false,
             std::string{prefix} + ".velocity_scale");
}

[[nodiscard]] bool parse_space_exit(
    Parser& parser,
    const io::DataValue& root,
    std::optional<SpaceExitDefinition>& out) {
  const io::DataValue* node = parser.member(root, "space_exit");
  if (node == nullptr) {
    return true;
  }
  if (!parser.require_object(*node, "space_exit")) {
    return false;
  }
  SpaceExitDefinition exit;
  if (!parser.read_bool(*node, "enabled", exit.enabled, false, "space_exit.enabled")) {
    return false;
  }
  const bool required_when_enabled = exit.enabled;
  if (!parser.read_ref(
          *node,
          "target_space_world",
          exit.target_space_world,
          required_when_enabled,
          "space_exit.target_space_world") ||
      !parser.read_i32(
          *node,
          "min_y",
          exit.min_y,
          required_when_enabled,
          "space_exit.min_y") ||
      !parser.read_f32(
          *node,
          "min_vertical_speed",
          exit.min_vertical_speed,
          required_when_enabled,
          "space_exit.min_vertical_speed") ||
      !parser.read_ref_list(
          *node,
          "require_vehicle_tags",
          exit.require_vehicle_tags,
          false,
          "space_exit.require_vehicle_tags") ||
      !parser.read_bool(
          *node,
          "consume_fuel",
          exit.consume_fuel,
          false,
          "space_exit.consume_fuel")) {
    return false;
  }
  if (const io::DataValue* spawn = parser.member(*node, "transition_spawn");
      spawn != nullptr) {
    if (!parser.require_object(*spawn, "space_exit.transition_spawn") ||
        !parse_transition_spawn(
            parser,
            *spawn,
            exit.transition_spawn,
            "space_exit.transition_spawn")) {
      return false;
    }
  }
  out = std::move(exit);
  return true;
}

[[nodiscard]] bool parse_space_world(
    Parser& parser,
    const io::DataValue& root,
    std::optional<SpaceWorldDefinition>& out) {
  const io::DataValue* node = parser.member(root, "space");
  if (node == nullptr) {
    return true;
  }
  if (!parser.require_object(*node, "space")) {
    return false;
  }
  SpaceWorldDefinition space;
  if (!parser.read_f32(
          *node,
          "coordinate_scale",
          space.coordinate_scale,
          false,
          "space.coordinate_scale") ||
      !parser.read_vec3(
          *node,
          "default_spawn_position",
          space.default_spawn_position,
          false,
          "space.default_spawn_position") ||
      !parser.read_vec3(
          *node,
          "default_spawn_rotation",
          space.default_spawn_rotation_degrees,
          false,
          "space.default_spawn_rotation") ||
      !parser.read_ref_list(*node, "bodies", space.bodies, false, "space.bodies")) {
    return false;
  }
  out = std::move(space);
  return true;
}

[[nodiscard]] bool parse_world_definition(
    Parser& parser,
    const io::DataValue& root,
    std::string_view id,
    SpaceDefinitionRegistry& out) {
  WorldDefinition definition;
  definition.id = std::string{id};
  if (!read_common(parser, root, id, definition.version) ||
      !read_world_kind(parser, root, definition.kind) ||
      !parser.read_string(root, "display_name", definition.display_name) ||
      !parser.read_ref(root, "environment", definition.environment) ||
      !parser.read_ref(
          root,
          "worldgen",
          definition.worldgen,
          definition.kind == WorldDefinitionKind::Planet) ||
      !parse_rules(parser, root, definition.rules) ||
      !parse_space_exit(parser, root, definition.space_exit) ||
      !parse_space_world(parser, root, definition.space)) {
    return false;
  }
  return insert_unique(
      parser,
      out.worlds,
      std::string{id},
      std::move(definition));
}

[[nodiscard]] bool parse_reentry(
    Parser& parser,
    const io::DataValue& root,
    EnvironmentReentryDefinition& out) {
  const io::DataValue* node = parser.member(root, "reentry");
  if (node == nullptr) {
    return true;
  }
  if (!parser.require_object(*node, "reentry")) {
    return false;
  }
  return parser.read_bool(*node, "enabled", out.enabled, false, "reentry.enabled") &&
         parser.read_string(*node, "effect", out.effect, false, "reentry.effect") &&
         parser.read_f32(*node, "drag_scale", out.drag_scale, false, "reentry.drag_scale") &&
         parser.read_bool(*node, "heat_damage", out.heat_damage, false, "reentry.heat_damage");
}

[[nodiscard]] bool parse_environment_definition(
    Parser& parser,
    const io::DataValue& root,
    std::string_view id,
    SpaceDefinitionRegistry& out) {
  EnvironmentDefinition definition;
  definition.id = std::string{id};
  if (!read_common(parser, root, id, definition.version) ||
      !parser.read_f32(root, "gravity_scale", definition.gravity_scale, false) ||
      !parser.read_bool(root, "has_atmosphere", definition.has_atmosphere, false) ||
      !parser.read_bool(root, "has_oxygen", definition.has_oxygen, false) ||
      !parser.read_string(root, "sky", definition.sky, false) ||
      !parser.read_f32(root, "ambient_light", definition.ambient_light, false) ||
      !parser.read_string(root, "fog", definition.fog, false) ||
      !parse_reentry(parser, root, definition.reentry)) {
    return false;
  }
  return insert_unique(
      parser,
      out.environments,
      std::string{id},
      std::move(definition));
}

[[nodiscard]] bool parse_render(
    Parser& parser,
    const io::DataValue& root,
    SpaceBodyRenderDefinition& out) {
  const io::DataValue* node = parser.member(root, "render");
  if (node == nullptr) {
    return parser.fail("render", "is required");
  }
  if (!parser.require_object(*node, "render")) {
    return false;
  }
  return parser.read_string(*node, "model", out.model, false, "render.model") &&
         parser.read_string(*node, "texture", out.texture, false, "render.texture") &&
         parser.read_bool(
             *node,
             "atmosphere_halo",
             out.atmosphere_halo,
             false,
             "render.atmosphere_halo");
}

[[nodiscard]] bool parse_body_entry(
    Parser& parser,
    const io::DataValue& root,
    SpaceBodyEntryDefinition& out) {
  const io::DataValue* node = parser.member(root, "entry");
  if (node == nullptr) {
    return parser.fail("entry", "is required");
  }
  if (!parser.require_object(*node, "entry")) {
    return false;
  }
  return parser.read_bool(*node, "enabled", out.enabled, false, "entry.enabled") &&
         read_entry_mode(parser, *node, out.mode, "entry.mode") &&
         parser.read_ref_list(
             *node,
             "require_vehicle_tags",
             out.require_vehicle_tags,
             false,
             "entry.require_vehicle_tags") &&
         parser.read_vec3(
             *node,
             "spawn_position",
             out.spawn_position,
             false,
             "entry.spawn_position") &&
         parser.read_vec3(
             *node,
             "spawn_velocity",
             out.spawn_velocity,
             false,
             "entry.spawn_velocity") &&
         parser.read_vec3(
             *node,
             "spawn_rotation",
             out.spawn_rotation_degrees,
             false,
             "entry.spawn_rotation") &&
         parser.read_string(*node, "effect", out.effect, false, "entry.effect");
}

[[nodiscard]] bool parse_space_body_definition(
    Parser& parser,
    const io::DataValue& root,
    std::string_view id,
    SpaceDefinitionRegistry& out) {
  SpaceBodyDefinition definition;
  definition.id = std::string{id};
  if (!read_common(parser, root, id, definition.version) ||
      !read_body_type(parser, root, definition.type) ||
      !parser.read_string(root, "display_name", definition.display_name) ||
      !parser.read_ref(root, "space_world", definition.space_world) ||
      !parser.read_ref(root, "target_world", definition.target_world, false) ||
      !parser.read_vec3(root, "position", definition.position) ||
      !parser.read_f32(root, "visual_radius", definition.visual_radius) ||
      !parser.read_f32(root, "entry_radius", definition.entry_radius) ||
      !parse_render(parser, root, definition.render) ||
      !parse_body_entry(parser, root, definition.entry)) {
    return false;
  }
  return insert_unique(
      parser,
      out.space_bodies,
      std::string{id},
      std::move(definition));
}

template<typename Fn>
[[nodiscard]] SpaceDefinitionRegistryResult load_category(
    const asset::PackManager& packs,
    std::string_view category,
    SpaceDefinitionRegistry registry,
    Fn&& parse_file) {
  for (const asset::PackDataFile& file : packs.data_files(category, ".yaml")) {
    const std::string id = derived_id_for(file);
    Parser parser;
    parser.id = id.empty() ? file.relative_path.generic_string() : id;
    if (id.empty()) {
      return SpaceDefinitionRegistryResult::failure(
          {parser.id, "path", "space definitions must live under data/<namespace>/"});
    }
    const std::string text = read_text_file(file.path);
    auto decoded = io::decode_yaml_value(text);
    if (!decoded) {
      return SpaceDefinitionRegistryResult::failure(
          {id, "root", "failed to parse YAML: " + decoded.error().describe()});
    }
    if (!parse_file(parser, decoded.value().root, id, registry)) {
      return SpaceDefinitionRegistryResult::failure(parser.error);
    }
  }
  return SpaceDefinitionRegistryResult::success(std::move(registry));
}

[[nodiscard]] bool validate_positive_radius(
    SpaceDefinitionDiagnostic& out,
    const SpaceBodyDefinition& body) {
  if (body.visual_radius <= 0.0F) {
    out = {body.id, "visual_radius", "must be greater than zero"};
    return false;
  }
  if (body.entry_radius <= 0.0F) {
    out = {body.id, "entry_radius", "must be greater than zero"};
    return false;
  }
  if (body.entry_radius < body.visual_radius) {
    out = {body.id, "entry_radius", "must be >= visual_radius"};
    return false;
  }
  return true;
}

[[nodiscard]] bool validate_registry(
    const SpaceDefinitionRegistry& registry,
    SpaceDefinitionDiagnostic& out) {
  for (const auto& [id, definition] : registry.worlds) {
    if (registry.find_environment(definition.environment) == nullptr) {
      out = {id, "environment", "references unknown environment '" + definition.environment + "'"};
      return false;
    }
    if (definition.kind == WorldDefinitionKind::Planet) {
      if (definition.worldgen.empty()) {
        out = {id, "worldgen", "is required for planet worlds"};
        return false;
      }
      if (!worldgen::valid_resource_id(definition.worldgen)) {
        out = {id, "worldgen", "must be a valid resource ID"};
        return false;
      }
      if (definition.space.has_value()) {
        out = {id, "space", "is only valid for space worlds"};
        return false;
      }
    } else {
      if (!definition.worldgen.empty()) {
        out = {id, "worldgen", "is only valid for planet worlds"};
        return false;
      }
      if (!definition.space.has_value()) {
        out = {id, "space", "is required for space worlds"};
        return false;
      }
      if (definition.space->coordinate_scale <= 0.0F) {
        out = {id, "space.coordinate_scale", "must be greater than zero"};
        return false;
      }
      for (const std::string& body_id : definition.space->bodies) {
        const SpaceBodyDefinition* body = registry.find_space_body(body_id);
        if (body == nullptr) {
          out = {id, "space.bodies", "references unknown space body '" + body_id + "'"};
          return false;
        }
        if (body->space_world != id) {
          out = {
              body_id,
              "space_world",
              "must match owning space world '" + id + "'"};
          return false;
        }
      }
    }
    if (definition.space_exit.has_value() && definition.space_exit->enabled) {
      const SpaceExitDefinition& exit = definition.space_exit.value();
      const WorldDefinition* target = registry.find_world(exit.target_space_world);
      if (target == nullptr) {
        out = {
            id,
            "space_exit.target_space_world",
            "references unknown world '" + exit.target_space_world + "'"};
        return false;
      }
      if (target->kind != WorldDefinitionKind::Space) {
        out = {
            id,
            "space_exit.target_space_world",
            "must reference a space world"};
        return false;
      }
      if (exit.target_space_world == id) {
        out = {id, "space_exit.target_space_world", "must not reference itself"};
        return false;
      }
      if (exit.min_vertical_speed < 0.0F) {
        out = {id, "space_exit.min_vertical_speed", "must be >= 0"};
        return false;
      }
      if (exit.transition_spawn.velocity_scale < 0.0F) {
        out = {id, "space_exit.transition_spawn.velocity_scale", "must be >= 0"};
        return false;
      }
    }
  }

  for (const auto& [id, body] : registry.space_bodies) {
    if (!validate_positive_radius(out, body)) {
      return false;
    }
    const WorldDefinition* space_world = registry.find_world(body.space_world);
    if (space_world == nullptr) {
      out = {id, "space_world", "references unknown world '" + body.space_world + "'"};
      return false;
    }
    if (space_world->kind != WorldDefinitionKind::Space) {
      out = {id, "space_world", "must reference a space world"};
      return false;
    }
    if (body.entry.enabled) {
      if (body.target_world.empty()) {
        out = {id, "target_world", "is required when entry is enabled"};
        return false;
      }
      const WorldDefinition* target = registry.find_world(body.target_world);
      if (target == nullptr) {
        out = {id, "target_world", "references unknown world '" + body.target_world + "'"};
        return false;
      }
      if (target->id == body.space_world) {
        out = {id, "target_world", "must not reference the containing space world"};
        return false;
      }
      if (body.entry.mode == SpaceBodyEntryMode::Disabled) {
        out = {id, "entry.mode", "must not be disabled when entry is enabled"};
        return false;
      }
    }
  }

  return true;
}

} // namespace

std::string SpaceDefinitionDiagnostic::describe() const {
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

const WorldDefinition* SpaceDefinitionRegistry::find_world(
    std::string_view id) const noexcept {
  const auto found = worlds.find(std::string{id});
  return found == worlds.end() ? nullptr : &found->second;
}

const EnvironmentDefinition* SpaceDefinitionRegistry::find_environment(
    std::string_view id) const noexcept {
  const auto found = environments.find(std::string{id});
  return found == environments.end() ? nullptr : &found->second;
}

const SpaceBodyDefinition* SpaceDefinitionRegistry::find_space_body(
    std::string_view id) const noexcept {
  const auto found = space_bodies.find(std::string{id});
  return found == space_bodies.end() ? nullptr : &found->second;
}

usize SpaceDefinitionRegistry::world_count() const noexcept {
  return worlds.size();
}

usize SpaceDefinitionRegistry::environment_count() const noexcept {
  return environments.size();
}

usize SpaceDefinitionRegistry::space_body_count() const noexcept {
  return space_bodies.size();
}

std::optional<WorldDefinitionKind> world_definition_kind_from_string(
    std::string_view value) noexcept {
  if (value == "planet") {
    return WorldDefinitionKind::Planet;
  }
  if (value == "space") {
    return WorldDefinitionKind::Space;
  }
  return std::nullopt;
}

std::optional<SpaceBodyType> space_body_type_from_string(
    std::string_view value) noexcept {
  if (value == "planet") {
    return SpaceBodyType::Planet;
  }
  if (value == "moon") {
    return SpaceBodyType::Moon;
  }
  if (value == "station") {
    return SpaceBodyType::Station;
  }
  if (value == "asteroid_field") {
    return SpaceBodyType::AsteroidField;
  }
  return std::nullopt;
}

std::optional<SpaceBodyEntryMode> space_body_entry_mode_from_string(
    std::string_view value) noexcept {
  if (value == "fixed_spawn") {
    return SpaceBodyEntryMode::FixedSpawn;
  }
  if (value == "directional_spawn") {
    return SpaceBodyEntryMode::DirectionalSpawn;
  }
  if (value == "docking_spawn") {
    return SpaceBodyEntryMode::DockingSpawn;
  }
  if (value == "disabled") {
    return SpaceBodyEntryMode::Disabled;
  }
  return std::nullopt;
}

SpaceDefinitionRegistryResult load_space_definition_registry_from_packs(
    const asset::PackManager& packs) {
  SpaceDefinitionRegistry registry;

  auto load_environments = load_category(
      packs,
      "environment",
      std::move(registry),
      [](Parser& parser,
         const io::DataValue& root,
         std::string_view id,
         SpaceDefinitionRegistry& out) {
        return parse_environment_definition(parser, root, id, out);
      });
  if (!load_environments) {
    return load_environments;
  }
  registry = std::move(load_environments.value());

  auto load_planet_worlds = load_category(
      packs,
      "world",
      std::move(registry),
      [](Parser& parser,
         const io::DataValue& root,
         std::string_view id,
         SpaceDefinitionRegistry& out) {
        return parse_world_definition(parser, root, id, out);
      });
  if (!load_planet_worlds) {
    return load_planet_worlds;
  }
  registry = std::move(load_planet_worlds.value());

  auto load_space_worlds = load_category(
      packs,
      "space",
      std::move(registry),
      [](Parser& parser,
         const io::DataValue& root,
         std::string_view id,
         SpaceDefinitionRegistry& out) {
        return parse_world_definition(parser, root, id, out);
      });
  if (!load_space_worlds) {
    return load_space_worlds;
  }
  registry = std::move(load_space_worlds.value());

  auto load_bodies = load_category(
      packs,
      "space_body",
      std::move(registry),
      [](Parser& parser,
         const io::DataValue& root,
         std::string_view id,
         SpaceDefinitionRegistry& out) {
        return parse_space_body_definition(parser, root, id, out);
      });
  if (!load_bodies) {
    return load_bodies;
  }
  registry = std::move(load_bodies.value());

  SpaceDefinitionDiagnostic diagnostic;
  if (!validate_registry(registry, diagnostic)) {
    return SpaceDefinitionRegistryResult::failure(std::move(diagnostic));
  }
  return SpaceDefinitionRegistryResult::success(std::move(registry));
}

} // namespace arv::world
