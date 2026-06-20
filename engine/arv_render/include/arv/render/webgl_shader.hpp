#pragma once

#include "arv/core/types.hpp"

#include <string>
#include <string_view>

namespace arv::render {

struct WebGLProgramLinkResult {
  u32 program{};
  std::string error{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return program != 0U;
  }
};

struct WebGLProgramSetLinkResult {
  u32 sky_program{};
  u32 chunk_program{};
  u32 color_program{};
  u32 entity_program{};
  u32 particle_program{};
  u32 ui_program{};
  std::string error{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return sky_program != 0U &&
           chunk_program != 0U &&
           color_program != 0U &&
           entity_program != 0U &&
           particle_program != 0U &&
           ui_program != 0U;
  }
};

[[nodiscard]] WebGLProgramLinkResult link_webgl_program(
    std::string_view label,
    const char* vertex_source,
    const char* fragment_source);

[[nodiscard]] WebGLProgramSetLinkResult link_default_webgl_programs();

} // namespace arv::render
