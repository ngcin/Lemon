# lemon-packager — 目录拷贝式出包 v1（M7a 批⑤；ADR-016 M6/D2/D7）
# 链 lemon-engine：复用 AssetIndex（索引/校验/manifest.pkg.json 导出）与
# BakeAudioFile（音频现烤）单源——"打包消费层与运行时读取层同源"（D2 拍板时
# Unity/Godot/Unreal 三家对照的行业不变量；Unreal RunUAT 即此形态）。
# 平台面（批⑦）：mac 闭包 = otool/install_name_tool/codesign（本机工具）；
# win 闭包 = PeImports 内置解析（D-7b：dumpbin 要 vcvars，弃外呼）——平台无关
# 编译，mac/win 同源。
add_executable(lemon-packager main.cpp PeImports.cpp)
target_compile_features(lemon-packager PRIVATE cxx_std_20)
target_link_libraries(lemon-packager PRIVATE lemon-engine nlohmann_json::nlohmann_json)
# 引擎字体随包拷贝（与 lemon-game 同源定义——包内 data/Fonts/ 主路径、源树回退）
target_compile_definitions(lemon-packager PRIVATE
  LEMON_ENGINE_FONT_DIR="${CMAKE_SOURCE_DIR}/Engine/Ui/Fonts")

# PE import 解析器单测（批⑦ B2）：合成最小 PE 断言 + 坏档三例；平台无关
# （mac 也跑——CI 双平台都吃这门 ctest）。
add_executable(lemon-packager-selftest selftest.cpp PeImports.cpp)
target_compile_features(lemon-packager-selftest PRIVATE cxx_std_20)
add_test(NAME pkg-pe-selftest COMMAND lemon-packager-selftest)
