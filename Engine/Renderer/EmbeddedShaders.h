// 构建期嵌入的 SPIR-V（cmake/CompileShaders.cmake 生成符号；全局作用域，与 spike 同约定）
#pragma once

extern const unsigned int lemon_spv_sprite_vert[];
extern const unsigned int lemon_spv_sprite_vert_count;
extern const unsigned int lemon_spv_sprite_frag[];
extern const unsigned int lemon_spv_sprite_frag_count;

// 光贴图（M8 批②，ADR-019）
extern const unsigned int lemon_spv_light_vert[];
extern const unsigned int lemon_spv_light_vert_count;
extern const unsigned int lemon_spv_light_frag[];
extern const unsigned int lemon_spv_light_frag_count;
extern const unsigned int lemon_spv_composite_vert[];
extern const unsigned int lemon_spv_composite_vert_count;
extern const unsigned int lemon_spv_composite_frag[];
extern const unsigned int lemon_spv_composite_frag_count;

// 后处理链（M8 批④，02 §4 FX Pass；vert 复用 composite）
extern const unsigned int lemon_spv_bloom_extract_frag[];
extern const unsigned int lemon_spv_bloom_extract_frag_count;
extern const unsigned int lemon_spv_bloom_down_frag[];
extern const unsigned int lemon_spv_bloom_down_frag_count;
extern const unsigned int lemon_spv_bloom_up_frag[];
extern const unsigned int lemon_spv_bloom_up_frag_count;
extern const unsigned int lemon_spv_fx_final_frag[];
extern const unsigned int lemon_spv_fx_final_frag_count;

// RmlUi 后端（M6b 批③a，ADR-014）
extern const unsigned int lemon_spv_rmlui_vert[];
extern const unsigned int lemon_spv_rmlui_vert_count;
extern const unsigned int lemon_spv_rmlui_color_frag[];
extern const unsigned int lemon_spv_rmlui_color_frag_count;
extern const unsigned int lemon_spv_rmlui_texture_frag[];
extern const unsigned int lemon_spv_rmlui_texture_frag_count;
