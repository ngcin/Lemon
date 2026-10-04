# lemon-game — 独立游戏入口（M7a 批④；ADR-016 M2 / ADR-005 同源双入口）
# 纪律：不链 editor-core/ImGui——"编辑器→内核→平台层"依赖向下的可执行证明。
# 仅 LEMON_BUILD_SCRIPTING=ON 时存在（lemon-csharp 接口目标携带 dotnet 产物
# 与 LEMON_SCRIPT_DIR 宏——GameEntry 运行期先 exe 旁 runtime/ 再回退构建树）。
add_executable(lemon-game GameEntry.cpp)
target_compile_features(lemon-game PRIVATE cxx_std_20)
target_link_libraries(lemon-game PRIVATE lemon-engine lemon-csharp SDL3::SDL3)
target_compile_definitions(lemon-game PRIVATE
  LEMON_ENGINE_FONT_DIR="${CMAKE_SOURCE_DIR}/Engine/Ui/Fonts")

# dev 形态暂存：dotnet 产物（Lemon.Entry/SDK + runtimeconfig）拷到 exe 旁 runtime/
# ——批⑤ packager 的同款落位；构建树内即可 `lemon-game --project demo/svr-test`。
add_custom_command(TARGET lemon-game POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:lemon-game>/runtime
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          ${LEMON_SCRIPT_DIR} $<TARGET_FILE_DIR:lemon-game>/runtime
  COMMENT "staging dotnet runtime next to lemon-game"
  VERBATIM)
