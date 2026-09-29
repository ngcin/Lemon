// Lemon 引擎 — 游戏 UI 子系统（M6b 批③a 呈现地基 / 批③b 字体与资产通道 / 批③c
// C# API 与波1 机制，ADR-014 D3）
// 职责：RmlUi 上下文生命周期 + 字体注册 + 内存/文件文档加载/显隐/热重载 +
// 贴图解析器门面 + 帧 Update/Render 驱动 + ops 应用（M2 模板克隆/响亮失败）+
// UiEvent 出队（M3）+ 输入喂入与让出查询（M7）。
// 纪律：本头零 RmlUi 类型（Pimpl 全在 UiSubsystem.cpp——RmlUi 类型不出 .cpp）。
// 演进：③d 模板迁移 / ③e 图鉴；波2 M5 悬停、M6 RT/帧序源、波3 M4 拖放。
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Renderer/RHI.h"
#include "Ui/UiBridge.h"

namespace lemon::ui {

/// 贴图桥解析器（批③b，ADR-014 M6 资产源）：source = RmlUi JoinPath 解析后的
/// 路径（③c 起另有 "guid:<16hex>" 协议——M6 GUID 直引，安装方同此门面）；
/// 命中则回填引擎纹理句柄 + 尺寸（外部纹理——后端只持描述符集，image 归
/// 图集/调用方）。安装方 = 编辑器（路径 → AssetDatabase → AtlasRegistry）。
using UiTextureResolver =
    std::function<bool(const std::string& source, rhi::Texture& tex, uint32_t& w, uint32_t& h)>;

/// 文档解析器（批③d 前置，通道 B / ADR-014 M1）：C# UI.Show 的文档名（relPath
/// 惯例）未装载时现场解析（relPath → absPath）供 ApplyOps 兜底装载——治时序与
/// 场景未声明的动态屏。安装方 = 编辑器（AssetDatabase FindByPath）；未安装的
/// 宿主（M8 前裸运行时）维持响亮失败（EditorAssetHooks 先例形态）。
using UiDocumentResolver =
    std::function<bool(const std::string& relPath, std::string& absPath)>;

/// 文档装载来源（R10 启用——2026-09-29 形态 D 收口：场景声明移除后的清场判据）：
/// Scene = 通道 A 场景声明装载（EnterPlay 扫描）；CSharp = 通道 B resolver 现载
/// （C# 动态 Show）；Edit = 编辑器双击预览/冒烟播种（③b「跨 Play 保持」承诺，
/// 清场豁免面）。重载（Reload*）不改来源——底稿变来源不变。
enum class UiDocOrigin : uint8_t { Scene = 0, CSharp, Edit };

/// M7 键盘喂入的引擎侧键位（自有枚举——头文件零 RmlUi；映射 RmlUi KI 在 .cpp）。
/// 波1 集合 = 焦点导航 + 单行文本编辑所需（字母/数字/方向/编辑键）。
enum class UiKey : uint8_t {
    None = 0, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Up, Down, Left, Right, Backspace, Return, Escape, Space, Home, End, Delete, Tab,
};

class UiSubsystem {
public:
    UiSubsystem(); // 定义在 cpp（Pimpl：内联 =default 会实例化 ~unique_ptr<Impl> 于
                   // Impl 不完整的 TU——构造异常路径需析构已构成员）
    ~UiSubsystem(); // 未 Shutdown = 断言（拥有者显式收尾，SpriteBatcher 同契约）
    UiSubsystem(const UiSubsystem&) = delete;
    UiSubsystem& operator=(const UiSubsystem&) = delete;

    /// 建后端 + RmlUi 初始化 + context("game"，注册 IME handler) + 系统字体链兜底；
    /// sdlWindow = SDL_Window*（文本输入 ActivateKeyboard→SDL_SetTextInputArea 用，
    /// 可空 = 文本输入降级 no-op）。false = 不可用（字体链全败等——UI 缺席不阻断）
    bool Init(rhi::Device& device, rhi::Format rtFormat, void* sdlWindow = nullptr);
    void Shutdown(); // Rml::Shutdown → backend.Shutdown（序：Rml 收尾期间仍会回调后端）

    /// 注册字体（批③b：引擎 Noto / 项目字体）。family = 调用方所知族名（报告/冒烟
    /// 断言用；RmlUi 按文件内名匹配——传错名渲染不受影响但报告失真）。fallback=true
    /// 时成功装载将更新 LoadedFontFamily（引擎正字优先）。
    bool LoadFontFace(const char* absPath, const char* family, bool fallback);

    /// 安装贴图桥解析器（须在文档加载前；文档重载即按新解析器重解）。
    void SetTextureResolver(UiTextureResolver fn);
    /// 安装文档解析器（通道 B 兜底装载；ApplyOps 的 Show op 未命中时消费）。
    void SetDocumentResolver(UiDocumentResolver fn);

    /// 内存文档（③a 冒烟通道）。同名覆盖旧文档。
    bool LoadDocumentFromMemory(const char* name, const char* rmlText,
                                UiDocOrigin origin = UiDocOrigin::Scene);
    /// 文件文档（批③b 资产通道）：absPath 为文档底稿——相对 href/src 按其目录解析
    /// （SystemInterface::JoinPath 覆写）。name 惯例 = 资产 relPath（热重载对账键）。
    /// origin = 装载来源（清场判据，见 UiDocOrigin；默认 Scene）。
    bool LoadDocumentFromFile(const char* name, const char* absPath,
                              UiDocOrigin origin = UiDocOrigin::Scene);
    /// 热重载（批③b）：重读底稿（file = 重读盘，内存 = 原文本）；shown 态保持。
    /// 全量重载前清 RmlUi 样式表缓存（Factory 按路径缓存 <link> 解析结果——不清
    /// 则重载后样式仍旧档，实测 2026-09-28）。成功后注入 DocumentReloaded 事件
    /// （M2：C# 重灌数据信号）。
    bool ReloadDocument(const char* name);
    /// 样式热重载（.rcss 变更）：逐文档 ReloadStyleSheet——保 DOM/元素状态（③c
    /// 数据重灌的底座）+ 清缓存重建样式；比全量重载轻。
    void ReloadStyleSheets();
    /// 全量重载（设备丢失路径：几何/纹理均需重编译）。
    void ReloadAllDocuments();
    /// 卸载单文档（资产删除路径）；全部卸载（切项目）。
    bool UnloadDocument(const char* name);
    void UnloadAllDocuments();

    /// 显隐（批③d 前置：modal = 场景声明态写入口——原本置 true 唯一路径是
    /// ApplyOps 的 C# Show op；尾加默认参零破坏既有调用）。show=true 时 Show 后
    /// PullToFront（层级序 = 最近 Show 序，D1 甲-轻量）。不置 stale（③b 双击
    /// 预览 / 通道 A 声明装载来源，§3 口径）。
    bool ShowDocument(const char* name, bool show, bool modal = false);
    bool HasDocument(const char* name) const;
    /// 状态对账用（2026-09-29 根因收口）：文件装载文档名全集——编辑器侧对账逐出
    /// 的枚举面（内存底稿文档不参与：无资产对应，逐出语义不适用）
    std::vector<std::string> FileBackedDocumentNames() const;
    /// 冒烟探针：文档 shown 态（查无 = false）——stale 归位断言源
    bool IsDocumentShown(const char* name) const;
    /// 批③d 前置（§3 EnterPlay 归位）：声明集之外的文档清理——**非 Edit 来源**
    /// （Scene 声明装载但本局未声明——实体已删等；CSharp 动态屏）→ Hide（装载
    /// 保留，下次 Show 免 IO）+ 清 stale；Edit 来源（双击预览）→ 保持现状（③b
    /// 预期延续）。声明集内的文档由调用方逐个 ShowDocument(name, showOnStart,
    /// modal) 归位到声明态（先于本调用）。2026-09-29 形态 D 收口：判据自 stale
    /// 位升级为来源标记（stale 位只覆盖 C# 动态 Show，漏"声明装载后实体被删"）。
    void ResetDynamicDocuments(const std::vector<std::string>& declared);
    /// 退 Play 清场（2026-09-29 形态 D 收口，Unity「Stop = 运行时态归零」同构）：
    /// 非 Edit 来源（Scene/CSharp）文档全部 Hide（装载保留免 IO）+ 清 stale；
    /// Edit 来源豁免（跨 Play 保持，③b）。返回隐藏数（观测日志用）。与
    /// ResetDynamicDocuments 幂等共存（EnterPlay 侧仍是兜底不变量）。
    uint32_t HideNonEditDocuments();

    // --------------------------------------------- 批③c：M2 ops 应用 / M3 事件 ----
    /// 单一提交口（C# UI.Apply 每帧一批；ScriptHost 拉取经 UiHooks 转入）。
    /// 解析失败 = 响亮失败（LEMON_ERROR + ContractErrorCount++），批内继续。
    void ApplyOps(const UiOpC* ops, uint32_t count, const char* arena, uint32_t arenaBytes);
    /// 事件出队（拷入调用方缓冲；返回条数）。UI 交互不入输入快照/StateHash。
    uint32_t DrainEvents(UiEventC* dst, uint32_t cap);
    /// 契约错误累计（id/容器/模板/字段不命中；smoke 断言"零契约错误"源）
    uint32_t ContractErrorCount() const;
    /// 冒烟探针：文档装载调用累计（FromMemory/FromFile 成功各 +1；通道 A/B 断言源
    /// ——无 UIDocument 场景恒 0 = 基准护栏，批③d 前置 §5）
    uint32_t DocumentLoadCount() const;
    /// M7 让出查询：任一 shown 文档存在焦点文本控件（引擎此刻吃键盘/文本输入）
    bool WantsKeyboard() const;
    /// M7 让出查询：任一 shown 文档带模态标记（"菜单打开时脚本让出输入"规则化）
    bool AnyModalShown() const;
    /// 事件探针：待派发事件数（冒烟/诊断）
    uint32_t PendingEventCount() const;

    // --------------------------------------------- 批③c：M7 输入喂入 ----
    /// 鼠标位（画布像素坐标，即 ctx 坐标——③a 定：ctx 尺寸 = gameRT 物理像素）；
    /// inside=false 时 UI 悬停失效
    void SetPointer(int x, int y, bool inside);
    /// 点击边沿（btn 0=左；down/up 成对）
    void ProcessMouseButton(int btn, bool down);
    /// 键盘边沿（每键 down/up 成对；WantsKeyboard 时引擎独占——EditorApp 已按
    /// 让出门过滤游戏侧）
    void ProcessKey(UiKey key, bool down);
    /// 提交制文本输入（SDL_TEXT_INPUT；激活 IME 时由 SDL 产生）
    void ProcessTextInput(const char* utf8);
    /// IME 预编辑（SDL_TEXT_EDITING；经 SdlTextInputHandler 走组合/提交路径）
    void ProcessTextEditing(const char* utf8, int start, int length);
    /// 候选窗锚点换算（M7：caret 是画布像素坐标，SDL_SetTextInputArea 要窗口点坐标
    /// ——窗口点 = origin + caret×scale；EditorApp 按 GameView 画布矩形每帧喂）
    void SetImeRectTransform(float originX, float originY, float scaleX, float scaleY);

    /// 帧逻辑步（Play 中、World 步进后调用——context Update；RmlUi 事件在此产生
    /// 并被文档级监听器收进事件队列）
    void Update();
    /// 帧呈现（gameRT 动态渲染块内、sprite 之后：尺寸变化 → SetDimensions，随后
    /// backend.BeginFrame → ctx Render → backend.EndFrame）
    void Render(rhi::CommandList& cl, uint32_t rtW, uint32_t rtH);

    /// 批③d-1（B1 dp 坐标系）：dp 参考高（px）。ratio = rtH/refH 只作用于 dp 单位
    /// 属性（px 值不动——既有文档零影响）；0 = 不缩放（默认，px=dp）。Render 内
    /// 变化才喂 ctx（RmlUi 原生 OnDpRatioChangeRecursive 触发全文档 dp 属性重排）。
    void SetDpReferenceHeight(uint32_t refH);
    uint32_t DpReferenceHeight() const; // 冒烟探针
    float DpRatio() const;              // 冒烟探针（当前生效 ratio；refH=0 → 1）

    /// 冒烟探针：实际载入的字体族名（空 = 未初始化）
    const char* LoadedFontFamily() const;

    /// 冒烟探针：文档内元素文本（GetElementById→inner text；查无 = 空串）——
    /// ops 生效断言的零渲染依赖读数
    bool TryGetElementText(const char* docName, const char* elementId, char* out,
                           uint32_t cap) const;
    /// 冒烟探针：模板容器当前条目数（SetItems 后克隆行数；查无容器 = -1）
    int ContainerItemCount(const char* docName, const char* containerId) const;
    /// 冒烟探针：条目克隆根中心（画布像素坐标——合成点击注入的落点；查无 = false）
    bool TryGetItemCenter(const char* docName, const char* containerId, const char* itemKey,
                          float* x, float* y) const;
    /// 冒烟探针：元素 Border 盒尺寸（px；查无 = false）——dp 坐标系端到端断言源
    /// （批③d-1：期望值 = dp 值 × DpRatio()）
    bool TryGetElementBox(const char* docName, const char* elementId, float* w,
                          float* h) const;
    /// 冒烟探针：元素浮点属性（progress value/max 等；缺属性/缺元素 = false）——
    /// 属性通道接线断言源（T8 后修②：<progress> 原生化后 fill 为引擎定位非 DOM
    /// 子元素，盒断言让位给属性回读 + 轨道盒）
    bool TryGetElementAttrF(const char* docName, const char* elementId,
                            const char* attr, float* out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lemon::ui
