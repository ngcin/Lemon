# 2026-09-24 Engine-Review Medium 批D：序列化与平台（M16/M17）

[Medium 排查轮](./2026-09-24-engine-review-medium-batch-a.md)第二批（A→D→B→C 顺序里
挑最轻的先落）。两条均零行为面变更、单点修复。

- **M16 场景名往返丢失**：`SceneArchive::Save` 写 `doc["name"]`，`Load` 从不读回
  ——存 "Level3.scene" 再打开，编辑器标题回默认名。修 = `Scene` 补 `SetName`（原
  name_ 仅构造器可置），Load 在实体段校验后恢复（旧档/无名档不覆盖调用方默认名）。
  **场景档字节零变化**（Save 侧原本就写 name），老档直接受益；测试侧原注释"场景名
  是宿主属性，不参与比较"是 M16 时代口径，已随修更正（名参与往返且同为不动点）。
- **M17 SDL drop 文件泄漏**：`SDL_EVENT_DROP_FILE` 的 `ev.drop.data` 归应用所有，
  SDL3 约定必须 `SDL_free`，原只拷贝不释放 = 每次拖入泄漏一个字符串。修 = 拷贝后
  释放（空串也占分配；data 是 `const char*`，所有权转移需去 const 转 `void*`——
  本机坑登记：这是 SDL3 与 SDL2 的签名差异之一）。

**回归**：engine-tests **13186 checks OK**（+1 场景名往返断言，roundtrip 不动点
用例原样通过）；`editor-regression.sh quick` **6/6**。拖入导入路径属手测项
（smoke-ui 的 rename/nav 段照跑无扰动），下次真人走查顺带覆盖。

剩余：批B（M10-M13 ScriptHost）→ 批C（M14/M15 C# 域生命周期与事件）。
