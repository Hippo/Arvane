#include "debug_imgui.hpp"

#include "arv/core/log.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

#include <glad/glad.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace arv::desktop {
namespace {

[[nodiscard]] bool glfw_key_down(GLFWwindow* window, int key) noexcept {
  if (window == nullptr) {
    return false;
  }
  return glfwGetKey(window, key) == GLFW_PRESS;
}

void add_key(GLFWwindow* window, ImGuiKey imgui_key, int glfw_key) {
  ImGui::GetIO().AddKeyEvent(imgui_key, glfw_key_down(window, glfw_key));
}

[[nodiscard]] GLuint compile_shader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint success = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (success != GL_TRUE) {
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

} // namespace

DebugImGuiLayer::~DebugImGuiLayer() {
  shutdown();
}

bool DebugImGuiLayer::initialize(GLFWwindow* window) {
  if (initialized_) {
    return true;
  }
  (void)window;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.BackendPlatformName = "arvane_glfw";
  io.BackendRendererName = "arvane_opengl3_render_thread";
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
  ImGui::StyleColorsDark();

  initialized_ = true;
  return true;
}

void DebugImGuiLayer::shutdown() {
  if (!initialized_ && ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::DestroyContext();
  }
  initialized_ = false;
}

DebugImGuiFontAtlas DebugImGuiLayer::font_atlas() const {
  DebugImGuiFontAtlas atlas;
  if (!initialized_ || ImGui::GetCurrentContext() == nullptr) {
    return atlas;
  }

  unsigned char* pixels = nullptr;
  int width = 0;
  int height = 0;
  ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return atlas;
  }

  atlas.width = width;
  atlas.height = height;
  atlas.rgba_pixels.assign(
      pixels,
      pixels + static_cast<usize>(width) * static_cast<usize>(height) * 4U);
  ImGui::GetIO().Fonts->SetTexID(DebugImGuiFontTextureID);
  return atlas;
}

void DebugImGuiLayer::begin_frame(
    GLFWwindow* window,
    const platform::InputSnapshot& input,
    platform::FramebufferSize framebuffer,
    Seconds delta_seconds) {
  if (!initialized_) {
    return;
  }
  update_input(window, input, framebuffer, delta_seconds);
  ImGui::NewFrame();
}

DebugImGuiDrawData DebugImGuiLayer::capture_draw_data() {
  DebugImGuiDrawData copied;
  if (!initialized_) {
    return copied;
  }

  ImGui::Render();
  const ImDrawData* draw_data = ImGui::GetDrawData();
  if (draw_data == nullptr) {
    return copied;
  }

  const int framebuffer_width =
      static_cast<int>(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
  const int framebuffer_height =
      static_cast<int>(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
  if (framebuffer_width <= 0 || framebuffer_height <= 0) {
    return copied;
  }

  copied.display_pos = draw_data->DisplayPos;
  copied.display_size = draw_data->DisplaySize;
  copied.framebuffer_scale = draw_data->FramebufferScale;
  copied.command_lists.reserve(static_cast<usize>(draw_data->CmdListsCount));
  for (int list_index = 0; list_index < draw_data->CmdListsCount; ++list_index) {
    const ImDrawList* source = draw_data->CmdLists[list_index];
    if (source == nullptr) {
      continue;
    }

    DebugImGuiDrawList list;
    list.vertices.assign(
        source->VtxBuffer.Data,
        source->VtxBuffer.Data + source->VtxBuffer.Size);
    list.indices.assign(
        source->IdxBuffer.Data,
        source->IdxBuffer.Data + source->IdxBuffer.Size);
    list.commands.reserve(static_cast<usize>(source->CmdBuffer.Size));
    for (const ImDrawCmd& command : source->CmdBuffer) {
      if (command.UserCallback != nullptr) {
        continue;
      }
      list.commands.push_back(
          {command.ClipRect,
           command.GetTexID(),
           command.ElemCount,
           command.IdxOffset,
           static_cast<i32>(command.VtxOffset)});
    }
    if (!list.commands.empty()) {
      copied.command_lists.emplace_back(std::move(list));
    }
  }
  return copied;
}

void DebugImGuiLayer::update_input(
    GLFWwindow* window,
    const platform::InputSnapshot& input,
    platform::FramebufferSize framebuffer,
    Seconds delta_seconds) {
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2{
      static_cast<float>(framebuffer.width),
      static_cast<float>(framebuffer.height)};
  io.DisplayFramebufferScale = ImVec2{1.0F, 1.0F};
  io.DeltaTime = delta_seconds > 0.0F ? delta_seconds : 1.0F / 60.0F;
  io.AddMousePosEvent(input.cursor_x, input.cursor_y);
  io.AddMouseButtonEvent(0, input.mouse_left_held);
  io.AddMouseButtonEvent(1, input.mouse_right_held);
  io.AddMouseWheelEvent(0.0F, input.mouse_wheel_y);
  if (!input.debug_menu_toggle_pressed && !input.text_input.empty()) {
    io.AddInputCharactersUTF8(input.text_input.c_str());
  }

  const bool ctrl =
      glfw_key_down(window, GLFW_KEY_LEFT_CONTROL) || glfw_key_down(window, GLFW_KEY_RIGHT_CONTROL);
  const bool shift =
      glfw_key_down(window, GLFW_KEY_LEFT_SHIFT) || glfw_key_down(window, GLFW_KEY_RIGHT_SHIFT);
  const bool alt =
      glfw_key_down(window, GLFW_KEY_LEFT_ALT) || glfw_key_down(window, GLFW_KEY_RIGHT_ALT);
  io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
  io.AddKeyEvent(ImGuiMod_Shift, shift);
  io.AddKeyEvent(ImGuiMod_Alt, alt);

  add_key(window, ImGuiKey_Tab, GLFW_KEY_TAB);
  add_key(window, ImGuiKey_LeftArrow, GLFW_KEY_LEFT);
  add_key(window, ImGuiKey_RightArrow, GLFW_KEY_RIGHT);
  add_key(window, ImGuiKey_UpArrow, GLFW_KEY_UP);
  add_key(window, ImGuiKey_DownArrow, GLFW_KEY_DOWN);
  add_key(window, ImGuiKey_PageUp, GLFW_KEY_PAGE_UP);
  add_key(window, ImGuiKey_PageDown, GLFW_KEY_PAGE_DOWN);
  add_key(window, ImGuiKey_Home, GLFW_KEY_HOME);
  add_key(window, ImGuiKey_End, GLFW_KEY_END);
  add_key(window, ImGuiKey_Insert, GLFW_KEY_INSERT);
  add_key(window, ImGuiKey_Delete, GLFW_KEY_DELETE);
  add_key(window, ImGuiKey_Backspace, GLFW_KEY_BACKSPACE);
  add_key(window, ImGuiKey_Space, GLFW_KEY_SPACE);
  add_key(window, ImGuiKey_Enter, GLFW_KEY_ENTER);
  add_key(window, ImGuiKey_Escape, GLFW_KEY_ESCAPE);
  add_key(window, ImGuiKey_A, GLFW_KEY_A);
  add_key(window, ImGuiKey_C, GLFW_KEY_C);
  add_key(window, ImGuiKey_V, GLFW_KEY_V);
  add_key(window, ImGuiKey_X, GLFW_KEY_X);
  add_key(window, ImGuiKey_Y, GLFW_KEY_Y);
  add_key(window, ImGuiKey_Z, GLFW_KEY_Z);
}

DebugImGuiOpenGLRenderer::~DebugImGuiOpenGLRenderer() {
  shutdown();
}

bool DebugImGuiOpenGLRenderer::initialize(const DebugImGuiFontAtlas& font_atlas) {
  if (initialized_) {
    return true;
  }
  initialized_ = create_device_objects(font_atlas);
  if (!initialized_) {
    shutdown();
  }
  return initialized_;
}

void DebugImGuiOpenGLRenderer::shutdown() {
  destroy_device_objects();
  initialized_ = false;
}

void DebugImGuiOpenGLRenderer::render(const DebugImGuiDrawData& draw_data) {
  if (!initialized_ || draw_data.empty()) {
    return;
  }

  const int framebuffer_width =
      static_cast<int>(draw_data.display_size.x * draw_data.framebuffer_scale.x);
  const int framebuffer_height =
      static_cast<int>(draw_data.display_size.y * draw_data.framebuffer_scale.y);
  if (framebuffer_width <= 0 || framebuffer_height <= 0) {
    return;
  }

  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glEnable(GL_SCISSOR_TEST);
  glViewport(0, 0, framebuffer_width, framebuffer_height);

  const float left = draw_data.display_pos.x;
  const float right = draw_data.display_pos.x + draw_data.display_size.x;
  const float top = draw_data.display_pos.y;
  const float bottom = draw_data.display_pos.y + draw_data.display_size.y;
  const float projection[4][4] = {
      {2.0F / (right - left), 0.0F, 0.0F, 0.0F},
      {0.0F, 2.0F / (top - bottom), 0.0F, 0.0F},
      {0.0F, 0.0F, -1.0F, 0.0F},
      {(right + left) / (left - right), (top + bottom) / (bottom - top), 0.0F, 1.0F},
  };

  glUseProgram(shader_program_);
  glUniform1i(texture_uniform_, 0);
  glUniformMatrix4fv(projection_uniform_, 1, GL_FALSE, &projection[0][0]);
  glBindVertexArray(vertex_array_);

  const ImVec2 clip_offset = draw_data.display_pos;
  const ImVec2 clip_scale = draw_data.framebuffer_scale;
  for (const DebugImGuiDrawList& command_list : draw_data.command_lists) {
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(command_list.vertices.size() * sizeof(ImDrawVert)),
        command_list.vertices.data(),
        GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(command_list.indices.size() * sizeof(ImDrawIdx)),
        command_list.indices.data(),
        GL_STREAM_DRAW);

    for (const DebugImGuiDrawCommand& command : command_list.commands) {
      if (command.texture_id != DebugImGuiFontTextureID) {
        if (unknown_texture_warning_count_ == 0U) {
          core::warn(
              "debug_ui",
              "skipping ImGui draw command with unknown texture id {}",
              static_cast<unsigned long long>(command.texture_id));
        }
        ++unknown_texture_warning_count_;
        continue;
      }

      const ImVec2 clip_min{
          (command.clip_rect.x - clip_offset.x) * clip_scale.x,
          (command.clip_rect.y - clip_offset.y) * clip_scale.y};
      const ImVec2 clip_max{
          (command.clip_rect.z - clip_offset.x) * clip_scale.x,
          (command.clip_rect.w - clip_offset.y) * clip_scale.y};
      if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) {
        continue;
      }

      glScissor(
          static_cast<int>(clip_min.x),
          static_cast<int>(static_cast<float>(framebuffer_height) - clip_max.y),
          static_cast<int>(clip_max.x - clip_min.x),
          static_cast<int>(clip_max.y - clip_min.y));
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, font_texture_);
      glDrawElementsBaseVertex(
          GL_TRIANGLES,
          static_cast<GLsizei>(command.elem_count),
          sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
          reinterpret_cast<void*>(
              static_cast<intptr_t>(command.idx_offset * sizeof(ImDrawIdx))),
          static_cast<GLint>(command.vtx_offset));
    }
  }

  glBindVertexArray(0);
  glUseProgram(0);
  glDisable(GL_SCISSOR_TEST);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
}

bool DebugImGuiOpenGLRenderer::create_device_objects(
    const DebugImGuiFontAtlas& font_atlas) {
  if (font_atlas.rgba_pixels.empty() || font_atlas.width <= 0 || font_atlas.height <= 0) {
    return false;
  }

  static constexpr const char* VertexShaderSource = R"glsl(
#version 330 core
uniform mat4 u_projection;
layout (location = 0) in vec2 a_position;
layout (location = 1) in vec2 a_uv;
layout (location = 2) in vec4 a_color;
out vec2 v_uv;
out vec4 v_color;
void main() {
  v_uv = a_uv;
  v_color = a_color;
  gl_Position = u_projection * vec4(a_position.xy, 0.0, 1.0);
}
)glsl";
  static constexpr const char* FragmentShaderSource = R"glsl(
#version 330 core
uniform sampler2D u_texture;
in vec2 v_uv;
in vec4 v_color;
out vec4 o_color;
void main() {
  o_color = v_color * texture(u_texture, v_uv);
}
)glsl";

  vertex_shader_ = compile_shader(GL_VERTEX_SHADER, VertexShaderSource);
  fragment_shader_ = compile_shader(GL_FRAGMENT_SHADER, FragmentShaderSource);
  if (vertex_shader_ == 0 || fragment_shader_ == 0) {
    return false;
  }

  shader_program_ = glCreateProgram();
  glAttachShader(shader_program_, vertex_shader_);
  glAttachShader(shader_program_, fragment_shader_);
  glLinkProgram(shader_program_);
  GLint success = GL_FALSE;
  glGetProgramiv(shader_program_, GL_LINK_STATUS, &success);
  if (success != GL_TRUE) {
    return false;
  }

  texture_uniform_ = glGetUniformLocation(shader_program_, "u_texture");
  projection_uniform_ = glGetUniformLocation(shader_program_, "u_projection");

  glGenTextures(1, &font_texture_);
  glBindTexture(GL_TEXTURE_2D, font_texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(
      GL_TEXTURE_2D,
      0,
      GL_RGBA,
      font_atlas.width,
      font_atlas.height,
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      font_atlas.rgba_pixels.data());
  glBindTexture(GL_TEXTURE_2D, 0);

  glGenVertexArrays(1, &vertex_array_);
  glGenBuffers(1, &vertex_buffer_);
  glGenBuffers(1, &index_buffer_);

  glBindVertexArray(vertex_array_);
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(
      0,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(ImDrawVert),
      reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
  glVertexAttribPointer(
      1,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(ImDrawVert),
      reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
  glVertexAttribPointer(
      2,
      4,
      GL_UNSIGNED_BYTE,
      GL_TRUE,
      sizeof(ImDrawVert),
      reinterpret_cast<void*>(offsetof(ImDrawVert, col)));
  glBindVertexArray(0);
  return true;
}

void DebugImGuiOpenGLRenderer::destroy_device_objects() {
  if (font_texture_ != 0) {
    glDeleteTextures(1, &font_texture_);
    font_texture_ = 0;
  }
  if (index_buffer_ != 0) {
    glDeleteBuffers(1, &index_buffer_);
    index_buffer_ = 0;
  }
  if (vertex_buffer_ != 0) {
    glDeleteBuffers(1, &vertex_buffer_);
    vertex_buffer_ = 0;
  }
  if (vertex_array_ != 0) {
    glDeleteVertexArrays(1, &vertex_array_);
    vertex_array_ = 0;
  }
  if (shader_program_ != 0) {
    glDeleteProgram(shader_program_);
    shader_program_ = 0;
  }
  if (vertex_shader_ != 0) {
    glDeleteShader(vertex_shader_);
    vertex_shader_ = 0;
  }
  if (fragment_shader_ != 0) {
    glDeleteShader(fragment_shader_);
    fragment_shader_ = 0;
  }
}

} // namespace arv::desktop
