# SPIR-V 字节 → C++ uint32 数组源文件（脚本模式运行：cmake -DIN=.. -DOUT=.. -DSYMBOL=.. -P）
# 注意：file(READ HEX) 是字节序列；小端机上 uint32 字内 4 字节需倒序
# （字节 03 02 23 07 → uint32 0x07230203）。目标平台 x86/ARM64 均为小端。
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" hexlen)

set(words "")
set(i 0)
# 注意：CMake 4.x CMP0130 下 while() 不接受 "<" 符号比较，须用 LESS 等关键字
while(i LESS hexlen)
  string(SUBSTRING "${hex}" ${i} 8 w)
  string(SUBSTRING "${w}" 6 2 b3)
  string(SUBSTRING "${w}" 4 2 b2)
  string(SUBSTRING "${w}" 2 2 b1)
  string(SUBSTRING "${w}" 0 2 b0)
  if(words STREQUAL "")
    set(words "0x${b3}${b2}${b1}${b0}")
  else()
    string(APPEND words ",0x${b3}${b2}${b1}${b0}")
  endif()
  math(EXPR i "${i} + 8")
endwhile()

math(EXPR n "${hexlen} / 8")

file(WRITE "${OUT}"
"#include <cstdint>
// 由 cmake/SpvToCpp.cmake 生成，勿手改
extern const unsigned int ${SYMBOL}[] = { ${words} };
extern const unsigned int ${SYMBOL}_count = ${n};
")
