# GLSL → SPIR-V → 嵌入式 C++ 数组
# 用法：
#   lemon_embed_shader(<!target> <shader 文件相对路径>)   # 如 shaders/tri.vert
# 生成符号：lemon_spv_<name>_<stage>[] 与 lemon_spv_<name>_<stage>_count
#   （tri.vert → lemon_spv_tri_vert / lemon_spv_tri_vert_count）
# 着色器统一 target-env vulkan1.1（MoltenVK 与 Win 原生 1.3 均兼容；用到 1.3 特性时再升）

find_program(GLSLANG_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/bin)

function(lemon_embed_shader target shader_src)
  if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found (brew install glslang / Vulkan SDK)")
  endif()

  get_filename_component(dir_rel "${shader_src}" DIRECTORY)
  get_filename_component(base "${shader_src}" NAME_WE)
  get_filename_component(stage "${shader_src}" LAST_EXT)
  string(SUBSTRING "${stage}" 1 -1 stage)          # .vert -> vert
  set(sym "lemon_spv_${base}_${stage}")

  set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/spv")
  set(spv "${out_dir}/${base}.${stage}.spv")
  set(gen_cpp "${out_dir}/${sym}.cpp")

  add_custom_command(
    OUTPUT "${spv}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
    COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.1 -o "${spv}" "${CMAKE_CURRENT_SOURCE_DIR}/${shader_src}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader_src}"
    COMMENT "glslang ${shader_src}"
    VERBATIM)

  add_custom_command(
    OUTPUT "${gen_cpp}"
    COMMAND ${CMAKE_COMMAND}
            -DIN=${spv} -DOUT=${gen_cpp} -DSYMBOL=${sym}
            -P "${LEMON_CMAKE_DIR}/SpvToCpp.cmake"
    DEPENDS "${spv}" "${LEMON_CMAKE_DIR}/SpvToCpp.cmake"
    COMMENT "embed ${sym}"
    VERBATIM)

  target_sources(${target} PRIVATE "${gen_cpp}")
endfunction()
