#include "arv/types.hpp"

namespace arv {

ArvStringView view(std::string_view text) noexcept {
  return {text.data(), text.size()};
}

std::string_view view(ArvStringView text) noexcept {
  return text.data == nullptr ? std::string_view{} : std::string_view{text.data, text.size};
}

ArvBytesView bytes(std::span<const std::byte> data) noexcept {
  return {
      reinterpret_cast<const std::uint8_t*>(data.data()),
      data.size(),
  };
}

ArvBytesView bytes(std::string_view text) noexcept {
  return {
      reinterpret_cast<const std::uint8_t*>(text.data()),
      text.size(),
  };
}

} // namespace arv
