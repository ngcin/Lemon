# Lemon 编译缓存单源（M7c 批⓪ T3，2026-10-07）
# 六 preset 共享（mac/mac-debug/win/win-debug/win-ci/win-share）——在 CPM 与所有
# add_subdirectory 之前 include，第三方 TU（SDL3/FreeType/RmlUi/stb）同样吃缓存。
# 策略：探测到才挂 launcher；探测不到 = 一行 WARNING 给安装配方，绝不阻塞
# configure（CI 与新机器零摩擦）。win-ci 显式旁路（preset 侧 LEMON_COMPILER_CACHE=off
# ——GitHub runner 无缓存服务端，sccache 空转有开销；win-share 继承 win-ci 同旁路）。
# 外部已设 CMAKE_<LANG>_COMPILER_LAUNCHER（如 CI 自带方案）时不接管。
option(LEMON_COMPILER_CACHE "Use ccache(macOS)/sccache(Windows) compiler launcher when available" ON)

if(LEMON_COMPILER_CACHE
   AND NOT DEFINED CMAKE_C_COMPILER_LAUNCHER
   AND NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
  if(CMAKE_HOST_APPLE)
    find_program(LEMON_CACHE_PROGRAM ccache)
    set(LEMON_CACHE_INSTALL_HINT "brew install ccache")
  elseif(CMAKE_HOST_WIN32)
    find_program(LEMON_CACHE_PROGRAM sccache)
    set(LEMON_CACHE_INSTALL_HINT "winget install Mozilla.sccache")
  else()
    set(LEMON_CACHE_PROGRAM "LEMON_CACHE_PROGRAM-NOTFOUND")
    set(LEMON_CACHE_INSTALL_HINT "ccache（各发行版包管理器）")
  endif()
  if(LEMON_CACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${LEMON_CACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${LEMON_CACHE_PROGRAM}")
    message(STATUS "Lemon compiler cache: ${LEMON_CACHE_PROGRAM}")
  else()
    message(WARNING "Lemon compiler cache not found —— 继续无缓存构建（安装：${LEMON_CACHE_INSTALL_HINT}）")
  endif()
endif()
