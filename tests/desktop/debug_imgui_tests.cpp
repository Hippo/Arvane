#include "debug_imgui.hpp"

#include <gtest/gtest.h>

#include <imgui.h>

namespace {

TEST(DebugImGuiLayerTests, CapturedDrawDataIsSelfOwnedAndUsesFontTextureSentinel) {
  arv::desktop::DebugImGuiLayer layer;
  ASSERT_TRUE(layer.initialize(nullptr));

  const arv::desktop::DebugImGuiFontAtlas atlas = layer.font_atlas();
  EXPECT_GT(atlas.width, 0);
  EXPECT_GT(atlas.height, 0);
  EXPECT_FALSE(atlas.rgba_pixels.empty());

  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2{320.0F, 200.0F};
  io.DisplayFramebufferScale = ImVec2{1.0F, 1.0F};
  io.DeltaTime = 1.0F / 60.0F;
  ImGui::NewFrame();
  ImGui::Begin("DebugCopyTest");
  ImGui::TextUnformatted("copied draw data");
  ImGui::End();
  ImGui::GetForegroundDrawList()->AddRectFilled(
      ImVec2{8.0F, 8.0F},
      ImVec2{48.0F, 48.0F},
      IM_COL32(255, 255, 255, 255));

  arv::desktop::DebugImGuiDrawData draw_data = layer.capture_draw_data();
  ASSERT_FALSE(draw_data.empty());
  ASSERT_FALSE(draw_data.command_lists.front().vertices.empty());
  ASSERT_FALSE(draw_data.command_lists.front().indices.empty());
  ASSERT_FALSE(draw_data.command_lists.front().commands.empty());

  bool saw_font_texture = false;
  for (const arv::desktop::DebugImGuiDrawList& list : draw_data.command_lists) {
    for (const arv::desktop::DebugImGuiDrawCommand& command : list.commands) {
      if (command.texture_id == arv::desktop::DebugImGuiFontTextureID) {
        saw_font_texture = true;
      }
    }
  }
  EXPECT_TRUE(saw_font_texture);

  const ImDrawVert first_vertex = draw_data.command_lists.front().vertices.front();
  layer.shutdown();
  EXPECT_EQ(draw_data.command_lists.front().vertices.front().col, first_vertex.col);
}

} // namespace
