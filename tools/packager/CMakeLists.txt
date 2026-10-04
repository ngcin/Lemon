# lemon-packager — 目录拷贝式出包 v1（M7a 批⑤；ADR-016 M6/D2/D7）
# 链 lemon-engine：复用 AssetIndex（索引/校验/manifest.pkg.json 导出）与
# BakeAudioFile（音频现烤）单源——"打包消费层与运行时读取层同源"（D2 拍板时
# Unity/Godot/Unreal 三家对照的行业不变量；Unreal RunUAT 即此形态）。
# 仅 mac 平台自检（otool/install_name_tool/codesign）——win 路径批⑦ 补
# （dumpbin 依赖闭环，07 §3.5 随批勾销）。
add_executable(lemon-packager main.cpp)
target_compile_features(lemon-packager PRIVATE cxx_std_20)
target_link_libraries(lemon-packager PRIVATE lemon-engine nlohmann_json::nlohmann_json)
# 引擎字体随包拷贝（与 lemon-game 同源定义——包内 data/Fonts/ 主路径、源树回退）
target_compile_definitions(lemon-packager PRIVATE
  LEMON_ENGINE_FONT_DIR="${CMAKE_SOURCE_DIR}/Engine/Ui/Fonts")
