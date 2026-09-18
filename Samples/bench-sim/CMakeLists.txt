# bench-sim — M2 验收官（08 §3：1 万怪全系统模拟 ≤8ms/步，无渲染）
add_executable(lemon-bench-sim main.cpp)
target_link_libraries(lemon-bench-sim PRIVATE lemon-engine)
