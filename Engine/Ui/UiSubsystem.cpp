// Lemon 引擎 — 游戏 UI 子系统实现（M6a 批③a/③b/③c，ADR-014）
// RmlUi 类型只准出现在本 .cpp（头文件零泄漏，同 Vulkan 纪律形状）。
#include "Ui/UiSubsystem.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <deque>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/TextInputContext.h>
#include <RmlUi/Core/TextInputHandler.h>

#include "Core/Log.h"
#include "Renderer/RmlUiBackend.h"
#include "Ui/SdlTextInputHandler.h"

namespace lemon::ui {
namespace {

// ---- 供本文件的小工具 ----------------------------------------------------------

// RmlUi inner-RML 语境的文本转义（SetText 走 SetInnerRML——&<> 必须实体化）
std::string EscapeText(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
    return out;
}

// "{{field}}" 占位提取（模板 class 插值契约，纸面验证 ⓪/① 形态）
void CollectPlaceholderTokens(const std::string& text, std::set<std::string>& out) {
    size_t at = 0;
    while ((at = text.find("{{", at)) != std::string::npos) {
        const size_t end = text.find("}}", at + 2);
        if (end == std::string::npos) break;
        const std::string token = text.substr(at + 2, end - at - 2);
        if (!token.empty()) out.insert(token);
        at = end + 2;
    }
}

std::string InterpolatePlaceholders(const std::string& text,
                                    const std::vector<std::pair<std::string, std::string>>& kv) {
    std::string out = text;
    for (const auto& [name, value] : kv) {
        std::string needle = "{{" + name + "}}";
        size_t at = 0;
        while ((at = out.find(needle, at)) != std::string::npos) {
            out.replace(at, needle.size(), value);
            at += value.size();
        }
    }
    return out;
}

bool IsHex16(const std::string& s) {
    if (s.size() != 16) return false;
    for (char c : s)
        if (!std::isxdigit((unsigned char)c)) return false;
    return true;
}

// UiKey → RmlUi KI（M7 波1 集合：字母/数字/方向/编辑键）
Rml::Input::KeyIdentifier MapKey(UiKey k) {
    using K = UiKey;
    using R = Rml::Input::KeyIdentifier;
    switch (k) {
    case K::A: case K::B: case K::C: case K::D: case K::E: case K::F: case K::G:
    case K::H: case K::I: case K::J: case K::K: case K::L: case K::M: case K::N:
    case K::O: case K::P: case K::Q: case K::R: case K::S: case K::T: case K::U:
    case K::V: case K::W: case K::X: case K::Y: case K::Z:
        return (R)((int)Rml::Input::KI_A + ((int)k - (int)K::A));
    case K::Num0: case K::Num1: case K::Num2: case K::Num3: case K::Num4:
    case K::Num5: case K::Num6: case K::Num7: case K::Num8: case K::Num9:
        return (R)((int)Rml::Input::KI_0 + ((int)k - (int)K::Num0));
    case K::Up: return Rml::Input::KI_UP;
    case K::Down: return Rml::Input::KI_DOWN;
    case K::Left: return Rml::Input::KI_LEFT;
    case K::Right: return Rml::Input::KI_RIGHT;
    case K::Backspace: return Rml::Input::KI_BACK;
    case K::Return: return Rml::Input::KI_RETURN;
    case K::Escape: return Rml::Input::KI_ESCAPE;
    case K::Space: return Rml::Input::KI_SPACE;
    case K::Home: return Rml::Input::KI_HOME;
    case K::End: return Rml::Input::KI_END;
    case K::Delete: return Rml::Input::KI_DELETE;
    case K::Tab: return Rml::Input::KI_TAB;
    default: return Rml::Input::KI_UNKNOWN;
    }
}

} // namespace

namespace {

struct LemonSystemInterface final : Rml::SystemInterface {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    void* sdlWindow = nullptr;        // SDL_Window*（文本输入候选窗定位；空 = 降级）
    float imeOx = 0, imeOy = 0, imeSx = 1, imeSy = 1; // 画布像素 → 窗口点换算（EditorApp 喂）

    double GetElapsedTime() override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            LEMON_ERROR("[rmlui] %s", message.c_str());
            break;
        case Rml::Log::LT_WARNING:
            LEMON_WARN("[rmlui] %s", message.c_str());
            break;
        default:
            LEMON_LOG("[rmlui] %s", message.c_str());
            break;
        }
        return true; // 截断 RmlUi 默认 stdout 输出（引擎 Console 为唯一汇聚）
    }

    // 批③b：POSIX 路径语义。上游默认把前导 '/' 剥掉按 cwd 解（RmlUi 的"根 = 数据
    // 根"约定）——本引擎以绝对路径装载文档，必须直通绝对路径；相对路径按文档目录
    // 拼接 + lexically_normal 吃 ".."。<link href> 与贴图 src 两路均经此口
    // （XMLNodeHandlerHead / RenderManager 摸底 2026-09-28）。
    void JoinPath(Rml::String& translated_path, const Rml::String& document_path,
                  const Rml::String& path) override {
        namespace fs = std::filesystem;
        if (!path.empty() && (path[0] == '/' || path.find(':') != Rml::String::npos)) {
            translated_path = path; // 绝对（POSIX / Windows 盘符 / "guid:" 协议）直通
            return;
        }
        if (document_path.empty()) {
            translated_path = path;
            return;
        }
        const fs::path joined = fs::path(document_path.c_str()).parent_path() / path.c_str();
        translated_path = joined.lexically_normal().generic_string();
    }

    // 批③c（M7/ADR-014 D4）：<input> 聚焦/光标移动 → RmlUi 自动回调（T7 实证每移
    // 随发）——候选窗贴光标 = SDL_SetTextInputArea(窗口点换算后的 caret 矩形)
    void ActivateKeyboard(Rml::Vector2f caret_position, float line_height) override {
        if (!sdlWindow) return;
        const SDL_Rect rect = {
            (int)(imeOx + caret_position.x * imeSx),
            (int)(imeOy + caret_position.y * imeSy),
            1, (int)(line_height * imeSy)};
        SDL_SetTextInputArea((SDL_Window*)sdlWindow, &rect, 0);
        SDL_StartTextInput((SDL_Window*)sdlWindow);
    }

    void DeactivateKeyboard() override {
        if (sdlWindow) SDL_StopTextInput((SDL_Window*)sdlWindow);
    }
};

// ③a 开发机字体链（spike-04 同款降级链；③b 换 Noto Sans CJK 随引擎/模板带，
// ADR-008 D2——正式版不依赖系统字体）
struct FontCandidate {
    const char* path;
    const char* family;
};
constexpr FontCandidate kFontChain[] = {
    {"/System/Library/Fonts/Hiragino Sans GB.ttc", "Hiragino Sans GB"},
    {"/System/Library/Fonts/STHeiti Medium.ttc", "Heiti SC"},
    {"/System/Library/Fonts/Songti.ttc", "Songti SC"},
};

} // namespace

struct UiSubsystem::Impl {
    std::unique_ptr<renderer::RmlUiBackend> backend = std::make_unique<renderer::RmlUiBackend>();
    LemonSystemInterface sys;
    SdlTextInputHandler ime;
    Rml::Context* ctx = nullptr;
    rhi::Device* device = nullptr;
    rhi::Device::RecreateCallbackId recreateCb = 0;
    uint32_t ctxW = 0, ctxH = 0;
    std::string fontFamily;
    bool reloadDocsNextUpdate = false; // 设备丢失 → 延迟到 Update 重载（见 Init 注记）

    // ---- 批③c：M2/M3 机制态 ----
    std::deque<UiEventC> events;      // 事件出队（Update 期由文档监听器产入）
    uint32_t contractErrors = 0;      // 响亮失败计数（smoke 断言零）
    bool pointerInside = false;

    // 模板容器状态（key = docName + "/" + containerId）。Doc 重载/卸载时整体失效。
    struct ContainerState {
        Rml::Element* container = nullptr;
        Rml::Element* tpl = nullptr;  // <template> 元素（display:none 持续压制）
        Rml::Element* proto = nullptr;// 行根原型（template 首个元素子）
        std::set<std::string> fields; // 模板字段集（data-field + class {{}} 占位）
        std::unordered_map<std::string, Rml::Element*> itemRoots; // 稳定 key → 克隆根
    };
    std::unordered_map<std::string, ContainerState> containers;

    struct Doc {
        std::string sourceText;         // 内存底稿（③a；两者互斥，file 优先）
        std::string sourcePath;         // 文件底稿（③b 资产通道；重读盘即热重载）
        Rml::ElementDocument* doc = nullptr;
        bool shown = false;
        bool modal = false;             // M1 模态标记（事件携带 + 让出面）
    };
    std::unordered_map<std::string, Doc> docs;

    // ---- 文档级事件监听（M3：无回调跨边界——监听器是引擎内部件）----
    struct DocListener final : Rml::EventListener {
        Impl* impl = nullptr;
        void ProcessEvent(Rml::Event& e) override { impl->OnRmlEvent(e); }
    };
    DocListener listener; // Impl 持有；attach 不转移所有权，Unload 时逐 doc detach

    void AttachListener(Doc& d) {
        if (!d.doc || !ctx) return;
        d.doc->AddEventListener(Rml::EventId::Click, &listener);
        d.doc->AddEventListener(Rml::EventId::Change, &listener);
        d.doc->AddEventListener(Rml::EventId::Submit, &listener);
    }
    void DetachListener(Doc& d) {
        if (!d.doc) return;
        d.doc->RemoveEventListener(Rml::EventId::Click, &listener);
        d.doc->RemoveEventListener(Rml::EventId::Change, &listener);
        d.doc->RemoveEventListener(Rml::EventId::Submit, &listener);
    }

    // 文档名反查（docs <10 线性足够；监听器统一挂各 doc，事件时定位源文档）
    Doc* DocByPtr(Rml::ElementDocument* p, std::string* nameOut = nullptr) {
        for (auto& [name, d] : docs)
            if (d.doc == p) {
                if (nameOut) *nameOut = name;
                return &d;
            }
        return nullptr;
    }

    // 事件身份解析：target 沿父链上行——
    //   data-event → 语义名；data-template 容器 → "容器id/条目data-key"；否则首个
    //   非空 id。Change 另带控件值（payload）。
    void OnRmlEvent(Rml::Event& e) {
        UiEventKind kind;
        switch (e.GetId()) {
        case Rml::EventId::Click: kind = UiEventKind::Click; break;
        case Rml::EventId::Change: kind = UiEventKind::Change; break;
        case Rml::EventId::Submit: kind = UiEventKind::Submit; break;
        default: return; // Hover 留位（M5 波2）
        }
        Rml::Element* target = e.GetTargetElement();
        if (!target) return;
        Rml::ElementDocument* ownerDoc = target->GetOwnerDocument();
        std::string docName;
        Doc* d = DocByPtr(ownerDoc, &docName);
        if (!d) return; // 非托管文档（编辑器侧另建）不进队列

        UiEventC ev{};
        ev.kind = (uint8_t)kind;
        ev.modal = d->modal ? 1 : 0;
        std::snprintf(ev.doc, sizeof(ev.doc), "%s", docName.c_str());

        std::string evName;
        Rml::Element* container = nullptr;
        Rml::Element* p = target;
        while (p && p != ownerDoc) {
            if (evName.empty()) {
                const Rml::String de = p->GetAttribute<Rml::String>("data-event", "");
                if (!de.empty()) evName = de;
            }
            const Rml::String dt = p->GetAttribute<Rml::String>("data-template", "");
            if (!dt.empty()) { container = p; break; }
            p = p->GetParentNode();
        }
        if (container) {
            // 条目行：target 上行到容器直接子（克隆根，挂 data-key）
            Rml::Element* row = target;
            while (row && row->GetParentNode() != container) row = row->GetParentNode();
            const Rml::String key = row ? row->GetAttribute<Rml::String>("data-key", "")
                                        : Rml::String();
            std::snprintf(ev.key, sizeof(ev.key), "%s/%s", container->GetId().c_str(),
                          key.c_str());
        } else {
            p = target;
            while (p && p != ownerDoc && p->GetId().empty()) p = p->GetParentNode();
            std::snprintf(ev.key, sizeof(ev.key), "%s", p ? p->GetId().c_str() : "");
        }
        std::snprintf(ev.ev, sizeof(ev.ev), "%s",
                      kind == UiEventKind::Submit ? "submit" : evName.c_str());
        if (kind == UiEventKind::Change) {
            if (Rml::ElementFormControl* fc =
                    dynamic_cast<Rml::ElementFormControl*>(target))
                std::snprintf(ev.payload, sizeof(ev.payload), "%s", fc->GetValue().c_str());
        }
        if (events.size() < 256) events.push_back(ev); // 低频点击级；溢出丢弃防洪水
    }

    // ---- M2 应用面 ----
    void ContractFail(const char* fmt, ...) {
        ++contractErrors;
        // LEMON_ERROR 变参宏不通用——组装后单参调用
        char buf[256];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        LEMON_ERROR("ui-contract: %s", buf);
    }

    const char* ArenaStr(const char* arena, uint32_t bytes, uint32_t off, const char* what) {
        if (off >= bytes) {
            ContractFail("arena 偏移越界（%s off=%u bytes=%u）", what, off, bytes);
            return "";
        }
        return arena + off; // NUL 终止契约（UiBridge.h）
    }

    Doc* FindDoc(const char* name) {
        auto it = docs.find(name);
        if (it == docs.end() || !it->second.doc) {
            ContractFail("document '%s' 未装载（ops 目标缺）", name);
            return nullptr;
        }
        return &it->second;
    }

    // key 双形解析：文档内元素 id ∥ "容器id/条目key"（SetItems 稳定 key 寻址）
    Rml::Element* ResolveKey(Doc& d, const std::string& docName, const std::string& key) {
        const size_t slash = key.find('/');
        if (slash == std::string::npos) {
            Rml::Element* el = d.doc->GetElementById(Rml::String(key));
            if (!el) ContractFail("element '%s' not found in '%s'", key.c_str(), docName.c_str());
            return el;
        }
        const std::string containerId = key.substr(0, slash);
        const std::string itemKey = key.substr(slash + 1);
        auto it = containers.find(docName + "/" + containerId);
        if (it == containers.end()) {
            ContractFail("container '%s' 未建立（先 SetItems）", key.c_str());
            return nullptr;
        }
        auto row = it->second.itemRoots.find(itemKey);
        if (row == it->second.itemRoots.end()) {
            ContractFail("item key '%s' not in container '%s'", itemKey.c_str(),
                         containerId.c_str());
            return nullptr;
        }
        return row->second;
    }

    // 模板容器解析（惰性建态：首条 SetItems 定型；Doc 重载即失效重建）
    ContainerState* ResolveContainer(Doc& d, const std::string& docName,
                                     const std::string& containerId,
                                     const std::string& templateName) {
        const std::string ck = docName + "/" + containerId;
        auto it = containers.find(ck);
        if (it != containers.end()) {
            if (it->second.container && it->second.container->GetOwnerDocument() == d.doc)
                return &it->second;
            containers.erase(it); // 文档已换实例（重载）——旧元素句柄全死，重建
        }
        Rml::Element* container = d.doc->GetElementById(Rml::String(containerId));
        if (!container) {
            ContractFail("container '%s' not found in '%s'（SetItems 目标缺）",
                         containerId.c_str(), docName.c_str());
            return nullptr;
        }
        // 原型载体 = <ui-template data-name="X">（自有约定标签）。RmlUi 原生
        // <template> 被其模板注入机制占用（XMLNodeHandlerTemplate：无 src 属性时
        // 子元素直接漏进父容器——原型根本不进 DOM），不可用（实现期发现，ADR 注记）
        Rml::Element* tpl = nullptr;
        for (int i = 0; i < container->GetNumChildren(); ++i) {
            Rml::Element* c = container->GetChild(i);
            if (c->GetTagName() == "ui-template" &&
                c->GetAttribute<Rml::String>("data-name", "") == Rml::String(templateName)) {
                tpl = c;
                break;
            }
        }
        if (!tpl) {
            ContractFail("template '%s' not in container '%s'（has data-name：见文档）",
                         templateName.c_str(), containerId.c_str());
            return nullptr;
        }
        Rml::Element* proto = nullptr;
        for (int i = 0; i < tpl->GetNumChildren(); ++i)
            if (!tpl->GetChild(i)->GetTagName().empty()) { proto = tpl->GetChild(i); break; }
        if (!proto) {
            ContractFail("template '%s' 无行根原型（首个元素子）", templateName.c_str());
            return nullptr;
        }
        tpl->SetProperty("display", "none"); // 原型不渲染（引擎压制，文档零约定负担）

        ContainerState cs;
        cs.container = container;
        cs.tpl = tpl;
        cs.proto = proto;
        // 字段集：data-field 名 + 全子树 class {{}} 占位（响亮失败的 "has:" 集合）
        std::vector<Rml::Element*> stack{proto};
        while (!stack.empty()) {
            Rml::Element* el = stack.back();
            stack.pop_back();
            const Rml::String f = el->GetAttribute<Rml::String>("data-field", "");
            if (!f.empty()) cs.fields.insert(f.c_str());
            const Rml::String cls = el->GetClassNames();
            if (cls.find("{{") != Rml::String::npos)
                CollectPlaceholderTokens(cls.c_str(), cs.fields);
            for (int i = 0; i < el->GetNumChildren(); ++i) stack.push_back(el->GetChild(i));
        }
        return &containers.emplace(ck, std::move(cs)).first->second;
    }

    void SetItems(Doc& d, const std::string& docName, const std::string& containerId,
                  const std::string& templateName, const uint8_t* rowsBlock, uint32_t rows,
                  uint32_t arenaBytes) {
        ContainerState* cs = ResolveContainer(d, docName, containerId, templateName);
        if (!cs) return;
        // 全量语义：清旧克隆行（template 元素保留）
        for (int i = cs->container->GetNumChildren() - 1; i >= 0; --i) {
            Rml::Element* c = cs->container->GetChild(i);
            if (c != cs->tpl) cs->container->RemoveChild(c);
        }
        cs->itemRoots.clear();

        const uint8_t* p = rowsBlock;
        const uint8_t* arenaEnd = rowsBlock + arenaBytes;
        for (uint32_t r = 0; r < rows; ++r) {
            // 行块解码：u8 keyLen | key | u16 字段数 | (u8 名长 | u16 值长 | 字节)*
            auto need = [&](size_t n) -> bool {
                if (p + n > arenaEnd) {
                    ContractFail("SetItems 行块越界（row %u/%s）", r, containerId.c_str());
                    return false;
                }
                return true;
            };
            if (!need(1)) return;
            const uint8_t keyLen = *p++;
            if (!need(keyLen + 2)) return;
            const std::string key((const char*)p, keyLen);
            p += keyLen;
            uint16_t fieldCount;
            std::memcpy(&fieldCount, p, 2);
            p += 2;
            std::vector<std::pair<std::string, std::string>> kv;
            kv.reserve(fieldCount);
            for (uint16_t f = 0; f < fieldCount; ++f) {
                if (!need(1)) return;
                const uint8_t nameLen = *p++;
                if (!need(nameLen + 2)) return;
                const std::string fname((const char*)p, nameLen);
                p += nameLen;
                uint16_t valLen;
                std::memcpy(&valLen, p, 2);
                p += 2;
                if (!need(valLen)) return;
                const std::string fval((const char*)p, valLen);
                p += valLen;
                // 响亮失败：字段不在模板集（"has:" 提示，纸面验证 ⓪ 原案）——跳过该
                // 字段继续（半成品行可见 = 排错线索）
                if (!cs->fields.count(fname)) {
                    std::string has;
                    for (const auto& x : cs->fields) {
                        if (!has.empty()) has += ",";
                        has += x;
                    }
                    ContractFail("field '%s' not in template '%s' (has: %s)", fname.c_str(),
                                 templateName.c_str(), has.c_str());
                    continue;
                }
                kv.emplace_back(fname, fval);
            }
            // 克隆 + 填充
            Rml::Element* appended =
                cs->container->AppendChild(cs->proto->Clone());
            appended->SetAttribute("data-key", Rml::String(key));
            std::vector<Rml::Element*> stack{appended};
            while (!stack.empty()) {
                Rml::Element* el = stack.back();
                stack.pop_back();
                const Rml::String f = el->GetAttribute<Rml::String>("data-field", "");
                if (!f.empty()) {
                    const std::string* val = nullptr;
                    for (const auto& [n, v] : kv)
                        if (n == f.c_str()) { val = &v; break; }
                    if (val) {
                        if (el->GetTagName() == "img") {
                            // M6 资产源：GUID 直引（16hex → "guid:" 协议）∥ 相对路径
                            el->SetAttribute("src", Rml::String(
                                IsHex16(*val) ? "guid:" + *val : *val));
                        } else {
                            el->SetInnerRML(Rml::String(EscapeText(*val)));
                        }
                    }
                }
                const Rml::String cls = el->GetClassNames();
                if (cls.find("{{") != Rml::String::npos)
                    el->SetClassNames(
                        Rml::String(InterpolatePlaceholders(cls.c_str(), kv)));
                for (int i = 0; i < el->GetNumChildren(); ++i) stack.push_back(el->GetChild(i));
            }
            cs->itemRoots[key] = appended;
        }
    }

    void InvalidateContainers(const std::string& docName) {
        const std::string prefix = docName + "/";
        for (auto it = containers.begin(); it != containers.end();) {
            if (it->first.rfind(prefix, 0) == 0) it = containers.erase(it);
            else ++it;
        }
    }

    void PushReloadedEvent(const std::string& docName) {
        UiEventC ev{};
        ev.kind = (uint8_t)UiEventKind::DocumentReloaded;
        std::snprintf(ev.doc, sizeof(ev.doc), "%s", docName.c_str());
        if (events.size() < 256) events.push_back(ev);
    }

    void UnloadDoc(Doc& d) {
        if (d.doc) {
            DetachListener(d);
            d.doc->Close(); // 从 context 移除并销毁（几何随后走 ReleaseGeometry 延迟回收）
            d.doc = nullptr;
        }
    }

    // 底稿 → 文档实例（file 优先；均空 = 坏条目防御返回 nullptr）。设备丢失与
    // 热重载共用的唯一重载路径——shown/modal 态由调用方恢复，监听器重挂。
    Rml::ElementDocument* ReloadDoc(Doc& d, const std::string& name) {
        UnloadDoc(d);
        Rml::ElementDocument* doc = nullptr;
        if (!d.sourcePath.empty()) doc = ctx->LoadDocument(Rml::String(d.sourcePath));
        else doc = ctx->LoadDocumentFromMemory(Rml::String(d.sourceText), Rml::String(name));
        d.doc = doc;
        if (doc) AttachListener(d);
        InvalidateContainers(name);
        return doc;
    }
};

// ------------------------------------------------------------- 生命周期 ----
UiSubsystem::UiSubsystem() = default;
UiSubsystem::~UiSubsystem() {
    // Run() 的早退路径（--save-scene/--smoke-guid/看门狗 return 等）不经主收尾段——
    // 成员声明序保证此时 device/viewport 仍存活（gameUi_ 后声明先析构），防御性就地
    // 收尾（红字留痕；主路径仍应显式 Shutdown，序即契约）
    if (impl_) {
        LEMON_WARN("UiSubsystem 析构前未显式 Shutdown（早退路径）——就地收尾");
        Shutdown();
    }
}

bool UiSubsystem::Init(rhi::Device& device, rhi::Format rtFormat, void* sdlWindow) {
    impl_ = std::make_unique<Impl>();
    Impl& i = *impl_;
    i.device = &device;
    i.sys.sdlWindow = sdlWindow;
    i.backend->Init(device, rtFormat);
    Rml::SetSystemInterface(&i.sys);
    Rml::SetRenderInterface((Rml::RenderInterface*)i.backend->RenderInterfacePtr());
    if (!Rml::Initialise()) {
        LEMON_ERROR("ui-subsystem: Rml::Initialise 失败");
        return false;
    }
    i.listener.impl = &i;
    // IME handler 注册（T7 实证形态：Rml::CreateContext 第 4 参；handler 生命周期
    // 随 Impl 成员，活到 Rml::Shutdown）
    i.ctx = Rml::CreateContext("game", Rml::Vector2i(64, 64), nullptr,
                               (Rml::TextInputHandler*)i.ime.HandlerPtr());
    if (!i.ctx) {
        LEMON_ERROR("ui-subsystem: CreateContext 失败");
        return false;
    }
    for (const auto& c : kFontChain) {
        if (Rml::LoadFontFace(c.path)) {
            i.fontFamily = c.family;
            break;
        }
    }
    if (i.fontFamily.empty()) {
        LEMON_ERROR("ui-subsystem: 系统字体链全败（Hiragino/STHeiti/Songti）——"
                    "③b 将随引擎带 Noto Sans CJK；本会话 UI 层不可用");
        return false;
    }
    // 设备丢失：后端先重建（注册序在前），本层随后重生字体图集 + 按底稿重载文档
    // （文档重载 → RmlUi 重编译几何——池是新的，旧几何句柄已随设备死）。
    // 批③b 修正：重载延迟到下一帧 Update——asset-gpu（图集页重建）的回调注册在
    // 本层之后（OpenProjectPipeline 时刻），立即重载会让贴图桥解析到未重建的死纹理。
    i.recreateCb = device.AddRecreateCallback("ui-subsystem", [this](rhi::Device&) {
        Rml::ReleaseFontResources();
        impl_->reloadDocsNextUpdate = true;
    });
    return true;
}

void UiSubsystem::Shutdown() {
    if (!impl_) return;
    Impl& i = *impl_;
    if (i.recreateCb) i.device->RemoveRecreateCallback(i.recreateCb);
    // 序：Rml 收尾期间仍会回调 backend（ReleaseGeometry/Texture）——backend 必须活到
    // Rml::Shutdown 之后；context/doc 由 Rml::Shutdown 统一销毁（句柄此后不可再碰）
    Rml::Shutdown();
    i.backend->Shutdown();
    impl_.reset();
}

// ---------------------------------------------------------------- 字体 ----
bool UiSubsystem::LoadFontFace(const char* absPath, const char* family, bool fallback) {
    if (!impl_) return false;
    if (!Rml::LoadFontFace(Rml::String(absPath), fallback)) {
        LEMON_WARN("ui-subsystem: 字体装载失败（fallback=%d）：%s", (int)fallback, absPath);
        return false;
    }
    if (fallback && family && family[0]) {
        impl_->fontFamily = family; // 引擎正字/最近装载的兜底字（冒烟探针读它）
    }
    return true;
}

void UiSubsystem::SetTextureResolver(UiTextureResolver fn) {
    if (impl_) impl_->backend->SetTextureResolver(std::move(fn));
}

// ---------------------------------------------------------------- 文档 ----
bool UiSubsystem::LoadDocumentFromMemory(const char* name, const char* rmlText) {
    if (!impl_ || !impl_->ctx) return false;
    Impl& i = *impl_;
    auto it = i.docs.find(name);
    if (it != i.docs.end()) i.UnloadDoc(it->second);
    Rml::ElementDocument* doc = i.ctx->LoadDocumentFromMemory(Rml::String(rmlText),
                                                              Rml::String(name));
    if (!doc) {
        LEMON_ERROR("ui-subsystem: LoadDocumentFromMemory('%s') 解析失败", name);
        return false;
    }
    auto& entry = i.docs[name];
    entry.sourceText = rmlText;
    entry.sourcePath.clear(); // 底稿形态互斥（file → memory 覆盖）
    entry.doc = doc;
    entry.shown = false;
    entry.modal = false;
    i.AttachListener(entry);
    i.InvalidateContainers(name);
    return true;
}

bool UiSubsystem::LoadDocumentFromFile(const char* name, const char* absPath) {
    if (!impl_ || !impl_->ctx) return false;
    Impl& i = *impl_;
    auto it = i.docs.find(name);
    if (it != i.docs.end()) i.UnloadDoc(it->second);
    // RmlUi 默认 FileInterface（fopen 绝对路径）；文档 source URL = absPath →
    // 相对 href/src 经 JoinPath 按文档目录解析（打包期自定义 IO 归 M8）
    Rml::ElementDocument* doc = i.ctx->LoadDocument(Rml::String(absPath));
    if (!doc) {
        LEMON_ERROR("ui-subsystem: LoadDocument('%s') 解析/读盘失败", absPath);
        return false;
    }
    auto& entry = i.docs[name];
    entry.sourceText.clear();
    entry.sourcePath = absPath;
    entry.doc = doc;
    entry.shown = false;
    entry.modal = false;
    i.AttachListener(entry);
    i.InvalidateContainers(name);
    return true;
}

bool UiSubsystem::ReloadDocument(const char* name) {
    if (!impl_) return false;
    auto it = impl_->docs.find(name);
    if (it == impl_->docs.end()) return false;
    // <link> 样式表按路径缓存（Factory）——重载前先清，否则新文档仍配旧样式
    Rml::Factory::ClearStyleSheetCache();
    const bool shown = it->second.shown;
    Rml::ElementDocument* doc = impl_->ReloadDoc(it->second, name);
    if (doc && shown) doc->Show();
    it->second.shown = shown;
    if (doc) impl_->PushReloadedEvent(name); // M2：C# 重灌信号
    LEMON_LOG("ui-subsystem: 文档热重载 %s（%s）", name,
              it->second.sourcePath.empty() ? "内存底稿" : it->second.sourcePath.c_str());
    return doc != nullptr;
}

void UiSubsystem::ReloadStyleSheets() {
    if (!impl_) return;
    // 逐文档 ReloadStyleSheet：内部清缓存 + 重解析文档头样式并原位替换——DOM 与
    // 元素状态保持（对齐 ADR-014 M2 "DocumentReloaded → C# 重灌" 的轻量前半段）
    for (auto& [name, d] : impl_->docs) {
        if (!d.doc) continue;
        d.doc->ReloadStyleSheet();
        impl_->PushReloadedEvent(name);
    }
    LEMON_LOG("ui-subsystem: 样式热重载（%zu 文档，DOM/状态保持）", impl_->docs.size());
}

void UiSubsystem::ReloadAllDocuments() {
    if (!impl_) return;
    for (auto& [name, d] : impl_->docs) {
        const bool shown = d.shown;
        Rml::ElementDocument* doc = impl_->ReloadDoc(d, name);
        if (doc && shown) doc->Show();
        d.shown = shown;
        if (doc) impl_->PushReloadedEvent(name);
    }
}

bool UiSubsystem::UnloadDocument(const char* name) {
    if (!impl_) return false;
    auto it = impl_->docs.find(name);
    if (it == impl_->docs.end()) return false;
    impl_->UnloadDoc(it->second);
    impl_->InvalidateContainers(name);
    impl_->docs.erase(it);
    return true;
}

void UiSubsystem::UnloadAllDocuments() {
    if (!impl_) return;
    for (auto& [name, d] : impl_->docs) impl_->UnloadDoc(d);
    impl_->docs.clear();
    impl_->containers.clear();
}

bool UiSubsystem::ShowDocument(const char* name, bool show) {
    if (!impl_) return false;
    auto it = impl_->docs.find(name);
    if (it == impl_->docs.end() || !it->second.doc) return false;
    if (show) it->second.doc->Show();
    else it->second.doc->Hide();
    it->second.shown = show;
    return true;
}

bool UiSubsystem::HasDocument(const char* name) const {
    return impl_ && impl_->docs.count(name) > 0;
}

// ------------------------------------------------------- 批③c：M2/M3 ----
void UiSubsystem::ApplyOps(const UiOpC* ops, uint32_t count, const char* arena,
                           uint32_t arenaBytes) {
    if (!impl_ || !ops || !count) return;
    Impl& i = *impl_;
    for (uint32_t k = 0; k < count; ++k) {
        const UiOpC& op = ops[k];
        const std::string docName =
            i.ArenaStr(arena, arenaBytes, op.s0, "doc");
        Impl::Doc* d = i.FindDoc(docName.c_str());
        if (!d) continue;
        switch ((UiOpType)op.type) {
        case UiOpType::Show:
            d->modal = (op.flags & 1) != 0; // M1 模态标记（让出面/事件携带）
            d->doc->Show();
            d->shown = true;
            break;
        case UiOpType::Hide:
            d->modal = false;
            d->doc->Hide();
            d->shown = false;
            break;
        case UiOpType::SetText:
            if (Rml::Element* el = i.ResolveKey(*d, docName,
                    i.ArenaStr(arena, arenaBytes, op.s1, "key")))
                el->SetInnerRML(Rml::String(EscapeText(
                    i.ArenaStr(arena, arenaBytes, op.s2, "text"))));
            break;
        case UiOpType::SetAttr:
            if (Rml::Element* el = i.ResolveKey(*d, docName,
                    i.ArenaStr(arena, arenaBytes, op.s1, "key"))) {
                const char* attr = i.ArenaStr(arena, arenaBytes, op.s2, "attr");
                const std::string val = i.ArenaStr(arena, arenaBytes, op.s3, "value");
                // src 的 GUID 直引协议随 M6（同 SetItems img 字段路径）
                el->SetAttribute(Rml::String(attr), Rml::String(
                    std::string(attr) == "src" && IsHex16(val) ? "guid:" + val : val));
            }
            break;
        case UiOpType::SetClass:
            if (Rml::Element* el = i.ResolveKey(*d, docName,
                    i.ArenaStr(arena, arenaBytes, op.s1, "key")))
                el->SetClass(Rml::String(i.ArenaStr(arena, arenaBytes, op.s2, "class")),
                             (op.flags & 1) != 0);
            break;
        case UiOpType::SetStyle:
            if (Rml::Element* el = i.ResolveKey(*d, docName,
                    i.ArenaStr(arena, arenaBytes, op.s1, "key")))
                el->SetProperty(Rml::String(i.ArenaStr(arena, arenaBytes, op.s2, "prop")),
                                Rml::String(i.ArenaStr(arena, arenaBytes, op.s3, "value")));
            break;
        case UiOpType::SetInnerRml:
            if (Rml::Element* el = i.ResolveKey(*d, docName,
                    i.ArenaStr(arena, arenaBytes, op.s1, "key")))
                el->SetInnerRML(Rml::String(i.ArenaStr(arena, arenaBytes, op.s2, "rml")));
            break;
        case UiOpType::SetItems: {
            const uint32_t off = op.s3;
            if (off >= arenaBytes) {
                i.ContractFail("SetItems 行块偏移越界（off=%u bytes=%u）", off, arenaBytes);
                break;
            }
            i.SetItems(*d, docName, i.ArenaStr(arena, arenaBytes, op.s1, "container"),
                       i.ArenaStr(arena, arenaBytes, op.s2, "template"),
                       (const uint8_t*)(arena + off), op.i0, arenaBytes - off);
            break;
        }
        default:
            i.ContractFail("未知 op 类型 %u（线格式两侧不同步？）", (unsigned)op.type);
            break;
        }
    }
}

uint32_t UiSubsystem::DrainEvents(UiEventC* dst, uint32_t cap) {
    if (!impl_ || !dst || !cap) return 0;
    Impl& i = *impl_;
    const uint32_t n = (uint32_t)std::min<size_t>(i.events.size(), cap);
    for (uint32_t k = 0; k < n; ++k) dst[k] = i.events.front();
    i.events.erase(i.events.begin(), i.events.begin() + n);
    return n;
}

uint32_t UiSubsystem::ContractErrorCount() const {
    return impl_ ? impl_->contractErrors : 0;
}

bool UiSubsystem::WantsKeyboard() const {
    if (!impl_ || !impl_->ctx) return false;
    Rml::Element* f = impl_->ctx->GetFocusElement();
    if (!f) return false;
    const Rml::String& tag = f->GetTagName();
    return tag == "input" || tag == "textarea"; // 文本控件持有键盘（M7 让出）
}

bool UiSubsystem::AnyModalShown() const {
    if (!impl_) return false;
    for (const auto& [name, d] : impl_->docs)
        if (d.shown && d.modal) return true;
    return false;
}

uint32_t UiSubsystem::PendingEventCount() const {
    return impl_ ? (uint32_t)impl_->events.size() : 0;
}

// ------------------------------------------------------- 批③c：M7 输入 ----
void UiSubsystem::SetPointer(int x, int y, bool inside) {
    if (!impl_ || !impl_->ctx) return;
    Impl& i = *impl_;
    if (inside) {
        impl_->ctx->ProcessMouseMove(x, y, 0);
        i.pointerInside = true;
    } else if (i.pointerInside) {
        impl_->ctx->ProcessMouseLeave();
        i.pointerInside = false;
    }
}

void UiSubsystem::ProcessMouseButton(int btn, bool down) {
    if (!impl_ || !impl_->ctx) return;
    if (down) impl_->ctx->ProcessMouseButtonDown(btn, 0);
    else impl_->ctx->ProcessMouseButtonUp(btn, 0);
}

void UiSubsystem::ProcessKey(UiKey key, bool down) {
    if (!impl_ || !impl_->ctx) return;
    const Rml::Input::KeyIdentifier ki = MapKey(key);
    if (ki == Rml::Input::KI_UNKNOWN) return;
    if (down) impl_->ctx->ProcessKeyDown(ki, 0);
    else impl_->ctx->ProcessKeyUp(ki, 0);
}

void UiSubsystem::ProcessTextInput(const char* utf8) {
    if (!impl_ || !impl_->ctx || !utf8) return;
    impl_->ctx->ProcessTextInput(Rml::String(utf8));
}

void UiSubsystem::ProcessTextEditing(const char* utf8, int start, int length) {
    if (!impl_ || !utf8) return;
    impl_->ime.HandleTextEditing(utf8, start, length);
}

void UiSubsystem::SetImeRectTransform(float originX, float originY, float scaleX,
                                      float scaleY) {
    if (!impl_) return;
    impl_->sys.imeOx = originX;
    impl_->sys.imeOy = originY;
    impl_->sys.imeSx = scaleX;
    impl_->sys.imeSy = scaleY;
}

// ----------------------------------------------------------------- 帧 ----
void UiSubsystem::Update() {
    if (!impl_ || !impl_->ctx) return;
    if (impl_->reloadDocsNextUpdate) { // 设备丢失翌帧：图集/字体均已重建（见 Init 注记）
        impl_->reloadDocsNextUpdate = false;
        ReloadAllDocuments();
    }
    impl_->ctx->Update();
}

void UiSubsystem::Render(rhi::CommandList& cl, uint32_t rtW, uint32_t rtH) {
    if (!impl_ || !impl_->ctx) return;
    Impl& i = *impl_;
    if (rtW != i.ctxW || rtH != i.ctxH) {
        i.ctxW = rtW;
        i.ctxH = rtH;
        i.ctx->SetDimensions(Rml::Vector2i((int)rtW, (int)rtH));
    }
    i.backend->BeginFrame(cl, rtW, rtH);
    i.ctx->Render();
    i.backend->EndFrame();
}

const char* UiSubsystem::LoadedFontFamily() const {
    return impl_ ? impl_->fontFamily.c_str() : "";
}

// ------------------------------------------------------------- 冒烟探针 ----
bool UiSubsystem::TryGetElementText(const char* docName, const char* elementId, char* out,
                                    uint32_t cap) const {
    if (!impl_ || !out || !cap) return false;
    *out = '\0';
    auto it = impl_->docs.find(docName);
    if (it == impl_->docs.end() || !it->second.doc) return false;
    Rml::Element* el = it->second.doc->GetElementById(Rml::String(elementId));
    if (!el) return false;
    std::snprintf(out, cap, "%s", el->GetInnerRML().c_str());
    return true;
}

int UiSubsystem::ContainerItemCount(const char* docName, const char* containerId) const {
    if (!impl_) return -1;
    auto it = impl_->containers.find(std::string(docName) + "/" + containerId);
    return it == impl_->containers.end() ? -1 : (int)it->second.itemRoots.size();
}

bool UiSubsystem::TryGetItemCenter(const char* docName, const char* containerId,
                                   const char* itemKey, float* x, float* y) const {
    if (!impl_ || !x || !y) return false;
    auto it = impl_->containers.find(std::string(docName) + "/" + containerId);
    if (it == impl_->containers.end()) return false;
    auto row = it->second.itemRoots.find(itemKey);
    if (row == it->second.itemRoots.end()) return false;
    const Rml::Vector2f tl = row->second->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f sz = row->second->GetBox().GetSize(Rml::BoxArea::Border);
    *x = tl.x + sz.x / 2;
    *y = tl.y + sz.y / 2;
    return true;
}

} // namespace lemon::ui
