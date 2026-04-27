#include "arv/io/input_bindings.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

#include <yaml-cpp/yaml.h>

namespace arv::io {
namespace {

void apply_required_action_fallbacks(platform::InputBindings& bindings) {
  const platform::InputBindings defaults = platform::default_input_bindings();
  for (usize index = 0; index < static_cast<usize>(platform::LogicalAction::Count); ++index) {
    const auto action = static_cast<platform::LogicalAction>(index);
    if (!platform::is_required_logical_action(action)) {
      continue;
    }
    if (platform::bindings_for_action(bindings, action).empty()) {
      platform::bindings_for_action(bindings, action) =
          platform::bindings_for_action(defaults, action);
    }
  }
}

std::optional<platform::InputBinding> parse_input_binding_node(const YAML::Node& node) {
  if (!node || !node.IsMap()) {
    return std::nullopt;
  }

  const YAML::Node device_node = node["device"];
  if (!device_node || !device_node.IsScalar()) {
    return std::nullopt;
  }

  const auto device_type =
      platform::input_device_type_from_name(device_node.as<std::string>());
  if (!device_type.has_value()) {
    return std::nullopt;
  }

  const char* key_name = nullptr;
  switch (device_type.value()) {
    case platform::InputDeviceType::Keyboard:
      key_name = "key";
      break;
    case platform::InputDeviceType::MouseButton:
      key_name = "button";
      break;
    case platform::InputDeviceType::MouseWheel:
      key_name = "direction";
      break;
  }

  const YAML::Node input_node = node[key_name];
  if (!input_node || !input_node.IsScalar()) {
    return std::nullopt;
  }

  const auto input_code =
      platform::canonical_input_code(device_type.value(), input_node.as<std::string>());
  if (!input_code.has_value()) {
    return std::nullopt;
  }

  u8 modifiers = 0U;
  const YAML::Node modifiers_node = node["modifiers"];
  if (modifiers_node) {
    if (!modifiers_node.IsSequence()) {
      return std::nullopt;
    }
    for (const YAML::Node& modifier_node : modifiers_node) {
      if (!modifier_node.IsScalar()) {
        return std::nullopt;
      }
      const auto modifier =
          platform::input_modifier_from_name(modifier_node.as<std::string>());
      if (!modifier.has_value()) {
        return std::nullopt;
      }
      modifiers |= platform::input_modifier_mask(modifier.value());
    }
  }

  return platform::InputBinding{device_type.value(), input_code.value(), modifiers};
}

void emit_binding(YAML::Emitter& emitter, const platform::InputBinding& binding) {
  emitter << YAML::BeginMap;
  emitter << YAML::Key << "device" << YAML::Value
          << std::string{platform::input_device_type_name(binding.device_type)};
  switch (binding.device_type) {
    case platform::InputDeviceType::Keyboard:
      emitter << YAML::Key << "key" << YAML::Value << binding.input_code;
      break;
    case platform::InputDeviceType::MouseButton:
      emitter << YAML::Key << "button" << YAML::Value << binding.input_code;
      break;
    case platform::InputDeviceType::MouseWheel:
      emitter << YAML::Key << "direction" << YAML::Value << binding.input_code;
      break;
  }

  if (binding.modifiers != 0U) {
    emitter << YAML::Key << "modifiers" << YAML::Value << YAML::BeginSeq;
    for (platform::InputModifier modifier :
         {platform::InputModifier::Shift, platform::InputModifier::Control, platform::InputModifier::Alt}) {
      if (platform::has_input_modifier(binding.modifiers, modifier)) {
        emitter << std::string{platform::input_modifier_name(modifier)};
      }
    }
    emitter << YAML::EndSeq;
  }
  emitter << YAML::EndMap;
}

} // namespace

InputBindingsResult parse_input_bindings_yaml(std::string_view text) {
  platform::InputBindings bindings;
  try {
    const YAML::Node root = YAML::Load(std::string{text});
    if (root && !root.IsNull()) {
      const YAML::Node version = root["version"];
      if (version && version.IsScalar()) {
        bindings.version = version.as<u32>();
      }

      const YAML::Node bindings_node = root["bindings"];
      if (bindings_node && bindings_node.IsMap()) {
        for (const auto& entry : bindings_node) {
          if (!entry.first.IsScalar()) {
            continue;
          }

          const auto action =
              platform::logical_action_from_name(entry.first.as<std::string>());
          if (!action.has_value()) {
            continue;
          }

          const YAML::Node action_bindings = entry.second;
          if (!action_bindings || !action_bindings.IsSequence()) {
            continue;
          }

          auto& output = platform::bindings_for_action(bindings, action.value());
          for (const YAML::Node& binding_node : action_bindings) {
            const auto binding = parse_input_binding_node(binding_node);
            if (binding.has_value()) {
              output.push_back(std::move(binding.value()));
            }
          }
        }
      }
    }
  } catch (const YAML::Exception& ex) {
    return InputBindingsResult::failure(ConfigError{ex.what()});
  }

  apply_required_action_fallbacks(bindings);
  return InputBindingsResult::success(std::move(bindings));
}

InputBindingsResult load_input_bindings_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return InputBindingsResult::failure(
        ConfigError{"failed to open input bindings file: " + path.string()});
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return parse_input_bindings_yaml(buffer.str());
}

InputBindingsResult load_input_bindings_file_or_defaults(const std::filesystem::path& path) {
  if (path.empty()) {
    return InputBindingsResult::success(platform::default_input_bindings());
  }

  if (!std::filesystem::exists(path)) {
    const platform::InputBindings defaults = platform::default_input_bindings();
    (void)save_input_bindings_file(path, defaults);
    return InputBindingsResult::success(defaults);
  }

  auto loaded = load_input_bindings_file(path);
  if (loaded) {
    return loaded;
  }
  return InputBindingsResult::success(platform::default_input_bindings());
}

WriteConfigResult save_input_bindings_file(
    const std::filesystem::path& path,
    const platform::InputBindings& bindings) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return WriteConfigResult::failure(
        ConfigError{"failed to create input bindings directory: " + error.message()});
  }

  YAML::Emitter emitter;
  emitter << YAML::BeginMap;
  emitter << YAML::Key << "version" << YAML::Value << bindings.version;
  emitter << YAML::Key << "bindings" << YAML::Value << YAML::BeginMap;
  for (usize index = 0; index < static_cast<usize>(platform::LogicalAction::Count); ++index) {
    const auto action = static_cast<platform::LogicalAction>(index);
    const auto& action_bindings = platform::bindings_for_action(bindings, action);
    if (action_bindings.empty()) {
      continue;
    }

    emitter << YAML::Key << std::string{platform::logical_action_name(action)};
    emitter << YAML::Value << YAML::BeginSeq;
    for (const platform::InputBinding& binding : action_bindings) {
      emit_binding(emitter, binding);
    }
    emitter << YAML::EndSeq;
  }
  emitter << YAML::EndMap;
  emitter << YAML::EndMap;

  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return WriteConfigResult::failure(
        ConfigError{"failed to open input bindings file for write: " + path.string()});
  }
  output << emitter.c_str();
  output.flush();
  if (!output) {
    return WriteConfigResult::failure(
        ConfigError{"failed to write input bindings file: " + path.string()});
  }
  return WriteConfigResult::success();
}

} // namespace arv::io
