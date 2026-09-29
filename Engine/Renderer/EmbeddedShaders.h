// 构建期嵌入的 SPIR-V（cmake/CompileShaders.cmake 生成符号；全局作用域，与 spike 同约定）
#pragma once

extern const unsigned int lemon_spv_sprite_vert[];
extern const unsigned int lemon_spv_sprite_vert_count;
extern const unsigned int lemon_spv_sprite_frag[];
extern const unsigned int lemon_spv_sprite_frag_count;

// RmlUi 后端（M6b 批③a，ADR-014）
extern const unsigned int lemon_spv_rmlui_vert[];
extern const unsigned int lemon_spv_rmlui_vert_count;
extern const unsigned int lemon_spv_rmlui_color_frag[];
extern const unsigned int lemon_spv_rmlui_color_frag_count;
extern const unsigned int lemon_spv_rmlui_texture_frag[];
extern const unsigned int lemon_spv_rmlui_texture_frag_count;
