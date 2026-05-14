#include "arv/io/input_bindings.hpp"

#include "arv/io/data_codecs.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

namespace arv::io {
namespace {

void apply_default_action_fallbacks(platform::InputBindings& bindings) {
  const platform::InputBindings defaults = platform::default_input_bindings();
  for (usize index = 0; index < static_cast<usize>(platform::LogicalAction::Count); ++index) {
    const auto action = static_cast<platform::LogicalAction>(index);
    if (platform::bindings_for_action(bindings, action).empty()) {
      platform::bindings_for_action(bindings, action) =
          platform::bindings_for_action(defaults, action);
    }
    auto& action_bindings = platform::bindings_for_action(bindings, action);
    if (action_bindings.size() > 2U) {
      action_bindings.resize(2U);
    }
  }
}

std::optional<platform::InputBinding> parse_input_binding_value(const DataValue& node) {
  if (node.kind() != DataKind::Object) {
    return std::nullopt;
  }

  const DataValue* device_node = node.get("device");
  if (device_node == nullptr || device_node->kind() != DataKind::String) {
    return std::nullopt;
  }

  const auto device_type =
      platform::input_device_type_from_name(device_node->as_string_or({}));
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

  const DataValue* input_node = node.get(key_name);
  if (input_node == nullptr || input_node->kind() != DataKind::String) {
    return std::nullopt;
  }

  const auto input_code =
      platform::canonical_input_code(device_type.value(), input_node->as_string_or({}));
  if (!input_code.has_value()) {
    return std::nullopt;
  }

  u8 modifiers = 0U;
  const DataValue* modifiers_node = node.get("modifiers");
  if (modifiers_node != nullptr) {
    const DataValue::Array* modifier_values = modifiers_node->array_if();
    if (modifier_values == nullptr) {
      return std::nullopt;
    }
    for (const DataValue& modifier_node : *modifier_values) {
      if (modifier_node.kind() != DataKind::String) {
        return std::nullopt;
      }
      const auto modifier =
          platform::input_modifier_from_name(modifier_node.as_string_or({}));
      if (!modifier.has_value()) {
        return std::nullopt;
      }
      modifiers |= platform::input_modifier_mask(modifier.value());
    }
  }

  return platform::InputBinding{device_type.value(), input_code.value(), modifiers};
}

DataValue binding_value(const platform::InputBinding& binding) {
  DataValue value = DataValue::object();
  value.set("device", DataValue{platform::input_device_type_name(binding.device_type)});
  switch (binding.device_type) {
    case platform::InputDeviceType::Keyboard:
      value.set("key", DataValue{binding.input_code});
      break;
    case platform::InputDeviceType::MouseButton:
      value.set("button", DataValue{binding.input_code});
      break;
    case platform::InputDeviceType::MouseWheel:
      value.set("direction", DataValue{binding.input_code});
      break;
  }

  if (binding.modifiers != 0U) {
    DataValue modifiers = DataValue::array();
    for (platform::InputModifier modifier :
         {platform::InputModifier::Shift, platform::InputModifier::Control, platform::InputModifier::Alt}) {
      if (platform::has_input_modifier(binding.modifiers, modifier)) {
        modifiers.push(DataValue{platform::input_modifier_name(modifier)});
      }
    }
    value.set("modifiers", std::move(modifiers));
  }
  return value;
}

} // namespace

InputBindingsResult decode_input_bindings_yaml(std::string_view text) {
  platform::InputBindings bindings;
  auto decoded = decode_yaml_value(text);
  if (!decoded) {
    return InputBindingsResult::failure(
        ConfigError{"failed to parse input bindings YAML: " + decoded.error().describe()});
  }

  const DataValue& root = decoded.value().root;
  if (root.kind() != DataKind::Null) {
    const DataValue* version = root.get("version");
    if (version != nullptr && (version->kind() == DataKind::I64 || version->kind() == DataKind::U64)) {
      bindings.version = static_cast<u32>(version->as_u64_or(bindings.version));
    }

    const DataValue* bindings_node = root.get("bindings");
    const DataValue::Object* bindings_map =
        bindings_node == nullptr ? nullptr : bindings_node->object_if();
    if (bindings_map != nullptr) {
      for (const auto& [action_name, action_value] : *bindings_map) {
        const auto action = platform::logical_action_from_name(action_name);
        if (!action.has_value()) {
          continue;
        }

        const DataValue::Array* action_bindings = action_value.array_if();
        if (action_bindings == nullptr) {
          continue;
        }

        auto& output = platform::bindings_for_action(bindings, action.value());
        for (const DataValue& binding_node : *action_bindings) {
          const auto binding = parse_input_binding_value(binding_node);
          if (binding.has_value()) {
            output.push_back(binding.value());
          }
        }
      }
    }
  }

  apply_default_action_fallbacks(bindings);
  return InputBindingsResult::success(std::move(bindings));
}

TextFileResult encode_input_bindings_yaml(const platform::InputBindings& bindings) {
  DataValue root = DataValue::object();
  root.set("version", DataValue{static_cast<u64>(bindings.version)});
  DataValue bindings_node = DataValue::object();
  for (usize index = 0; index < static_cast<usize>(platform::LogicalAction::Count); ++index) {
    const auto action = static_cast<platform::LogicalAction>(index);
    const auto& action_bindings = platform::bindings_for_action(bindings, action);
    if (action_bindings.empty()) {
      continue;
    }

    DataValue action_values = DataValue::array();
    for (const platform::InputBinding& binding : action_bindings) {
      action_values.push(binding_value(binding));
    }
    bindings_node.set(std::string{platform::logical_action_name(action)}, std::move(action_values));
  }
  root.set("bindings", std::move(bindings_node));

  auto encoded = encode_yaml_value(root);
  if (!encoded) {
    return TextFileResult::failure(
        ConfigError{"failed to encode input bindings YAML: " + encoded.error().describe()});
  }
  return TextFileResult::success(std::move(encoded.value()));
}

} // namespace arv::io

namespace arv::io {

InputBindingsResult parse_input_bindings_yaml(std::string_view text) {
  return decode_input_bindings_yaml(text);
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

  auto encoded = encode_input_bindings_yaml(bindings);
  if (!encoded) {
    return WriteConfigResult::failure(encoded.error());
  }

  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return WriteConfigResult::failure(
        ConfigError{"failed to open input bindings file for write: " + path.string()});
  }
  output << encoded.value();
  output.flush();
  if (!output) {
    return WriteConfigResult::failure(
        ConfigError{"failed to write input bindings file: " + path.string()});
  }
  return WriteConfigResult::success();
}

} // namespace arv::io
