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
# 批⑨ 后修：原 POST_BUILD 只随 lemon-game 重链触发——SDK .cs 单独重编不重链 =
# runtime/ 陈旧（真机首例：新 API Events.PlayResetHook 缺方法 → GameFlow 静态
# 构造炸）。改 custom_target 每构建跑 copy_if_different（无变化 = 廉价 no-op），
# DEPENDS lemon-dotnet-asm 保证 dotnet 产物先就位。
add_custom_target(lemon-game-runtime ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:lemon-game>/runtime
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          ${LEMON_SCRIPT_DIR} $<TARGET_FILE_DIR:lemon-game>/runtime
  DEPENDS lemon-game lemon-dotnet-asm
  COMMENT "staging dotnet runtime next to lemon-game"
  VERBATIM)
