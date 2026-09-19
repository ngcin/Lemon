# M3 脚本桥测试（lemon-dotnet-asm 触发 dotnet build；lemon-csharp 传宏与链接）
add_executable(lemon-script-tests main.cpp)
add_dependencies(lemon-script-tests lemon-dotnet-asm)
target_link_libraries(lemon-script-tests PRIVATE lemon-csharp)

add_test(NAME script-tests COMMAND lemon-script-tests)
