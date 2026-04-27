#pragma once

#include "arv/core/expected.hpp"
#include "arv/io/config.hpp"
#include "arv/platform/input_bindings.hpp"

namespace arv::io {

using InputBindingsResult = Expected<platform::InputBindings, ConfigError>;
using WriteConfigResult = Expected<void, ConfigError>;

InputBindingsResult parse_input_bindings_yaml(std::string_view text);
InputBindingsResult load_input_bindings_file(const std::filesystem::path& path);
InputBindingsResult load_input_bindings_file_or_defaults(const std::filesystem::path& path);
WriteConfigResult save_input_bindings_file(
    const std::filesystem::path& path,
    const platform::InputBindings& bindings);

} // namespace arv::io
