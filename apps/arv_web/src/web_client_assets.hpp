#pragma once

namespace arv::web {

void clear_chunk_meshes();
void clear_web_ui_assets();
void clear_entity_render_assets();
void clear_block_render_assets();

[[nodiscard]] bool ensure_block_render_assets_loaded();
[[nodiscard]] bool ensure_entity_render_assets_loaded();

} // namespace arv::web
