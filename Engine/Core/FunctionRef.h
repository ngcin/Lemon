// Lemon 引擎 — 非拥有的可调用视图（ParallelFor / 查询回调等热路径传函数用）
// std::function 有类型擦除分配与间接开销；本视图仅持有指针，零分配，
// 代价是生命周期由调用方保证（仅用于同步调用栈内，不跨帧存储）。
#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace lemon {

template <typename Signature>
class FunctionRef;

template <typename Ret, typename... Args>
class FunctionRef<Ret(Args...)> {
public:
    FunctionRef() = default;

    template <typename F>
    requires(!std::is_same_v<std::decay_t<F>, FunctionRef>)
    FunctionRef(F&& f)
        : callable_(reinterpret_cast<const void*>(std::addressof(f))),
          invoke_([](const void* p, Args... args) -> Ret {
              return (*reinterpret_cast<const std::decay_t<F>*>(p))(
                  std::forward<Args>(args)...);
          }) {}

    Ret operator()(Args... args) const {
        return invoke_(callable_, std::forward<Args>(args)...);
    }

private:
    const void* callable_ = nullptr;
    Ret (*invoke_)(const void*, Args...) = nullptr;
};

} // namespace lemon
