#pragma once

#include "arv/core/types.hpp"
#include "arv/io/config.hpp"
#include "arv/net/transport.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace arv::desktop {

[[nodiscard]] char lowercase_ascii(char ch) noexcept;
[[nodiscard]] bool is_save_name_character(char ch) noexcept;
[[nodiscard]] std::string sanitize_save_name(std::string_view name);
[[nodiscard]] std::string unique_server_save_name(
    const io::EngineConfig& config,
    std::string_view display_name);
[[nodiscard]] u64 generated_world_seed() noexcept;
[[nodiscard]] u64 parse_or_generate_seed(std::string_view seed_text) noexcept;
[[nodiscard]] std::optional<net::NetworkAddress> parse_server_address(std::string_view address);

} // namespace arv::desktop
