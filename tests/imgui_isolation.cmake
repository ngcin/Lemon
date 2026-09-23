# M4.0 验收断言（M4.md §5）：ImGui 头文件只准出现在 Editor/。
# lemon-engine（Engine/ 树）任何源文件出现 imgui 引用即 FAIL。
# ctest 条目见 tests/CMakeLists.txt；新增绕行需先改 M4.md §3.2 并记 ADR。
file(GLOB_RECURSE _engine_sources
  "${CMAKE_CURRENT_LIST_DIR}/../Engine/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../Engine/*.h")
list(FILTER _engine_sources EXCLUDE REGEX ".*/Scripting/host/.*") # vendored dotnet 头，非 imgui

set(_violations "")
foreach(f IN LISTS _engine_sources)
  file(READ "${f}" _content)
  if(_content MATCHES "(include[^\n]*imgui|IMGUI_[A-Z]|ImGui::|ImGui[A-Z])")
    list(APPEND _violations "${f}")
  endif()
endforeach()

if(_violations)
  foreach(f IN LISTS _violations)
    message(FATAL_ERROR "[imgui-isolation] ImGui 泄漏进引擎内核: ${f}")
  endforeach()
endif()
message(STATUS "[imgui-isolation] Engine/ 树零 ImGui 引用（断言通过）")
