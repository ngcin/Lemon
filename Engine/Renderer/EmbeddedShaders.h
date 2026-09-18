// 构建期嵌入的 SPIR-V（cmake/CompileShaders.cmake 生成符号；全局作用域，与 spike 同约定）
#pragma once

extern const unsigned int lemon_spv_sprite_vert[];
extern const unsigned int lemon_spv_sprite_vert_count;
extern const unsigned int lemon_spv_sprite_frag[];
extern const unsigned int lemon_spv_sprite_frag_count;
