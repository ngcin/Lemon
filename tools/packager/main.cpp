// Lemon 引擎 — lemon-packager 目录拷贝式出包 v1（M7a 批⑤；ADR-016 M6/D2/D7）
// 形态：独立工具目标链 lemon-engine（复用 AssetIndex/BakeAudioFile 单源——
// "打包消费层与运行时读取层同源"的行业不变量，D2 拍板纪要）。
//
//   pkg/lemon-game        二进制 + dylib 闭包（@rpath/@loader_path 重锚 + ad-hoc 签名）
//   pkg/libMoltenVK.dylib + MoltenVK_icd.json   ICD 自举（GameEntry 按在场自设
//                          VK_ICD_FILENAMES；library_path 相对清单解析，随包可搬迁）
//   pkg/runtime/          dotnet publish self-contained（合成宿主工程引用 Game.csproj
//                          + Lemon.Entry——类库不能自含 publish，SDK 会拒；宿主产物剪除）
//   pkg/data/             = 项目数据根：直拷（排除 .lemon 生成物）+ .lemon/bin/Game.dll
//                          + .lemon/baked/audio/（现烤）+ .lemon/manifest.pkg.json
//                          （AssetIndex 导出，运行时快路径首查）+ Fonts/（Noto 随包）
//
// CLI：lemon-packager --project <dir> --runtime <engine-build-dir> --out <pkg> [--force]
//   --project  游戏项目根（含 project.lemon 与 Game/Game.csproj）
//   --runtime  引擎构建目录（build/mac——取 Engine/Entry/lemon-game 与 Scripting 托管件）
//   --out      出包根（须不存在或空；--force = 先清）
//   --force    出包根在场时整删重建
//
// 自检（RESULT pkg-selfcheck 行，回归口径）：otool 依赖闭环 + 关键件在场 +
// 文件清单对账（组装期记账 ⊆ 实走；runtime/ publish 树外零多出）。
// 依赖本机工具：otool / install_name_tool / codesign / dotnet（批⑦ win 换 dumpbin）。
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <unistd.h> // getpid（合成宿主工程临时目录名）

#include <nlohmann/json.hpp>

#include "Assets/AssetIndex.h"
#include "Assets/AssetTypes.h"
#include "Assets/ProjectFile.h"
#include "Audio/BakedClip.h"

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

// 与 GameEntry 装配同口径：程序化页白精灵(0)+位图字体(1)之后首个可用号。
// manifest 记账号低于此 = 坏账丢弃（LoadFromManifest 口径），导出前 Open 用同基。
constexpr uint32_t kSpriteIdBase = 2;
// guid 形引用的下界（组件 id/team 等小整数域与 u64 随机 guid 的分界；GameFlow
// 三同步后的 guid 十进制恒为全幅 u64——M7a 前置修复的实测口径）
constexpr uint64_t kGuidLikeFloor = 1ull << 40;

int g_errors = 0;

void Err(const std::string& msg) {
    ++g_errors;
    std::printf("[lemon-packager] ERROR: %s\n", msg.c_str());
}

std::string Quote(const fs::path& p) { return "'" + p.string() + "'"; }

// ---- 子进程（otool 读输出；其余只看退出码）----
struct CmdResult {
    int rc = -1;
    std::string out;
};

CmdResult Run(const std::string& cmd) {
    CmdResult r;
    FILE* f = ::popen(cmd.c_str(), "r"); // NOLINT(cert-env33-c) 工具目标
    if (!f) return r;
    char buf[4096];
    while (size_t n = std::fread(buf, 1, sizeof(buf), f)) r.out.append(buf, n);
    r.rc = ::pclose(f);
    return r;
}

bool RunOk(const std::string& cmd, const std::string& what) {
    const CmdResult r = Run(cmd);
    if (r.rc != 0) {
        Err(what + " 失败（rc=" + std::to_string(r.rc) + "）:\n  " + cmd);
        return false;
    }
    return true;
}

// otool -L 依赖清单（跳过首行 = 本体 install name；逐行取 " (" 前的引用串）
std::vector<std::string> OtoolDeps(const fs::path& macho) {
    std::vector<std::string> deps;
    const CmdResult r = Run("otool -L " + Quote(macho));
    if (r.rc != 0) {
        Err("otool -L 不可用：" + macho.string());
        return deps;
    }
    bool first = true;
    size_t pos = 0;
    while (pos < r.out.size()) {
        size_t nl = r.out.find('\n', pos);
        if (nl == std::string::npos) nl = r.out.size();
        std::string line = r.out.substr(pos, nl - pos);
        pos = nl + 1;
        // 去缩进
        const size_t nons = line.find_first_not_of(" \t");
        if (nons == std::string::npos) continue;
        line = line.substr(nons);
        if (first) { // 首行 = macho 头（本体 install name）
            first = false;
            continue;
        }
        const size_t cut = line.find(" (");
        if (cut != std::string::npos) deps.push_back(line.substr(0, cut));
    }
    return deps;
}

// otool -l 解析 LC_RPATH 条目（重锚卫生面：链接期残留的构建机路径——JDK rpath
// 会按序遮蔽 @loader_path，freetype 装错源即此故，DYLD_PRINT_LIBRARIES 实抓）
std::vector<std::string> RpathsOf(const fs::path& macho) {
    std::vector<std::string> rpaths;
    const CmdResult r = Run("otool -l " + Quote(macho));
    if (r.rc != 0) return rpaths;
    bool inRpath = false;
    size_t pos = 0;
    while (pos < r.out.size()) {
        size_t nl = r.out.find('\n', pos);
        if (nl == std::string::npos) nl = r.out.size();
        std::string line = r.out.substr(pos, nl - pos);
        pos = nl + 1;
        const size_t nons = line.find_first_not_of(" \t");
        if (nons == std::string::npos) continue;
        line = line.substr(nons);
        if (line.rfind("cmd ", 0) == 0 && line.find("LC_RPATH") != std::string::npos) {
            inRpath = true;
            continue;
        }
        if (inRpath && line.rfind("path ", 0) == 0) {
            const size_t off = line.find(" (offset");
            rpaths.push_back(off != std::string::npos ? line.substr(5, off - 5) : line.substr(5));
            inRpath = false;
        } else if (line.rfind("cmd ", 0) == 0) {
            inRpath = false;
        }
    }
    return rpaths;
}

bool IsSystemDep(const std::string& ref) {
    return ref.rfind("/usr/lib/", 0) == 0 || ref.rfind("/System/", 0) == 0;
}

std::string BaseName(const std::string& ref) {
    const size_t s = ref.find_last_of('/');
    return s == std::string::npos ? ref : ref.substr(s + 1);
}

// @rpath/@loader_path 引用解析（不含 JDK rpath——链接期残留的假路径，批⑤ 实抓
// 的隐式假设；freetype 落 brew 本体）。搜索序：包根（已收件）→ 构建树 exe 目录
// → /usr/local/lib → /usr/local/opt/*/lib（brew 符号链接族）。
fs::path ResolveRpathRef(const std::string& name, const fs::path& pkgRoot,
                         const fs::path& buildEntryDir) {
    std::error_code ec;
    for (const fs::path& dir : {pkgRoot, buildEntryDir, fs::path("/usr/local/lib")}) {
        const fs::path hit = dir / name;
        if (fs::exists(hit, ec)) return hit;
    }
    const fs::path opt = "/usr/local/opt";
    if (fs::is_directory(opt, ec))
        for (auto& e : fs::directory_iterator(opt, ec)) {
            const fs::path hit = e.path() / "lib" / name;
            if (fs::exists(hit, ec)) return hit;
        }
    return {};
}

// 引用串 → 本体路径（绝对 = 原样；@rpath/@loader_path = 搜索序）
fs::path ResolveRef(const std::string& ref, const fs::path& pkgRoot,
                    const fs::path& buildEntryDir) {
    if (ref.rfind("/@", 0) == 0 || ref.rfind('/', 0) == 0) {
        std::error_code ec;
        return fs::exists(fs::path(ref), ec) ? fs::path(ref) : fs::path();
    }
    const size_t slash = ref.find('/');
    if (slash == std::string::npos) return {};
    const std::string rest = ref.substr(slash + 1); // 剥 @rpath/ 或 @loader_path/
    return ResolveRpathRef(rest, pkgRoot, buildEntryDir);
}

// ---- 组装记账（清单对账面：自检步消费）----
std::set<std::string> g_written;
void Record(const fs::path& p) { g_written.insert(fs::absolute(p).generic_string()); }

bool CopyFileTo(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    if (!fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec)) {
        Err("拷贝失败：" + src.string() + " -> " + dst.string() + "（" + ec.message() + "）");
        return false;
    }
    Record(dst);
    return true;
}

// 递归拷贝目录（跳过 skip 顶层生成物；.meta 随文件走）
bool CopyTree(const fs::path& src, const fs::path& dst,
              const std::unordered_set<std::string>& skipTop) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(src, ec); it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) {
            Err("目录遍历失败：" + src.string() + "（" + ec.message() + "）");
            return false;
        }
        const std::string name = it->path().filename().string();
        // 顶层跳过集（.lemon 等生成物）只对第一层生效
        if (it.depth() == 0 && skipTop.count(name)) it.disable_recursion_pending();
        else if (it->is_regular_file(ec) &&
                 !CopyFileTo(it->path(), dst / std::filesystem::relative(it->path(), src, ec)))
            return false;
    }
    return true;
}

// ---- guid 形引用悬空扫描（.scene/.prefab/.anim/.override/.controller 对索引）----
void CheckDanglingRefs(const fs::path& projectRoot, const lemon::assets::AssetIndex& index) {
    std::unordered_set<uint64_t> known;
    for (const lemon::assets::IndexedEntry& e : index.Entries()) known.insert(e.guid);
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(projectRoot, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        const std::string ext = it->path().extension().string();
        if (ext != ".scene" && ext != ".prefab" && ext != ".anim" && ext != ".override" &&
            ext != ".controller")
            continue;
        std::ifstream f(it->path(), std::ios::binary);
        if (!f) continue;
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const Json doc = Json::parse(text, nullptr, /*allow_exceptions=*/false);
        if (doc.is_discarded()) continue; // 坏档归运行时装载红字，packager 不重复裁决
        // 深遍历：引用形键（spriteGuid/sourceAssetGuid 等——后缀 Guid 命名约定）且值
        // 为 u64 全幅号 → 对索引。裸 "guid" 键 = 实体身份号（components/Meta 与场景
        // 自身,非资产引用——实测 Main.scene/Player.prefab 的假阳性口径）不裁决
        std::function<void(const Json&)> walk = [&](const Json& node) {
            if (node.is_object()) {
                for (auto it2 = node.begin(); it2 != node.end(); ++it2) {
                    std::string key = it2.key();
                    for (char& c : key) c = (char)std::tolower((unsigned char)c);
                    if (key != "guid" && key.find("guid") != std::string::npos &&
                        it2.value().is_number_unsigned()) {
                        const uint64_t v = it2.value().get<uint64_t>();
                        if (v >= kGuidLikeFloor && !known.count(v))
                            Err("引用悬空：" +
                                std::filesystem::relative(it->path(), projectRoot, ec).string() +
                                " 字段 '" + it2.key() + "' 值 " + std::to_string(v) +
                                " 不在资产索引");
                    }
                    walk(it2.value());
                }
            } else if (node.is_array()) {
                for (const Json& el : node) walk(el);
            }
        };
        walk(doc);
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string projectArg, runtimeArg, outArg;
    bool force = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--project") && i + 1 < argc) projectArg = argv[++i];
        else if (!std::strcmp(argv[i], "--runtime") && i + 1 < argc) runtimeArg = argv[++i];
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) outArg = argv[++i];
        else if (!std::strcmp(argv[i], "--force")) force = true;
        else {
            std::printf("lemon-packager：未知参数 %s（--project/--runtime/--out/--force）\n",
                        argv[i]);
            return 2;
        }
    }
    if (projectArg.empty() || runtimeArg.empty() || outArg.empty()) {
        std::printf("用法：lemon-packager --project <dir> --runtime <engine-build-dir> "
                    "--out <pkg> [--force]\n");
        return 2;
    }
    std::error_code ec;
    const fs::path project = fs::absolute(fs::path(projectArg), ec);
    const fs::path buildDir = fs::absolute(fs::path(runtimeArg), ec);
    const fs::path out = fs::absolute(fs::path(outArg), ec);
    const fs::path gameExe = buildDir / "Engine" / "Entry" / "lemon-game";
    const fs::path buildEntryDir = gameExe.parent_path();
    const fs::path buildScriptDir = buildDir / "Scripting" / "dotnet";

    // ---- 1. 项目校验（fail-fast 红字；ADR-016 M6"最小面"）----
    const lemon::assets::ProjectFile pf = lemon::assets::LoadProjectFile(project.string());
    if (!pf.ok) {
        Err("project.lemon 不可用：" + project.string());
        return 1;
    }
    const std::string entryScene = lemon::assets::ResolveEntryScene(project.string(), pf);
    if (entryScene.empty() ||
        !fs::is_regular_file(project / entryScene, ec)) {
        Err("入口场景不可用（entryScene 未声明/不唯一/文件缺失）：" + project.string());
        return 1;
    }
    if (!fs::is_regular_file(project / "Game" / "Game.csproj", ec)) {
        Err("项目缺 Game/Game.csproj（packager 经此工程 dotnet publish）：" + project.string());
        return 1;
    }
    if (!fs::is_regular_file(gameExe, ec)) {
        Err("引擎构建树缺 lemon-game：" + gameExe.string() + "（--runtime 指向 build/mac）");
        return 1;
    }
    if (!fs::is_regular_file(buildScriptDir / "Lemon.Entry.dll", ec)) {
        Err("引擎构建树缺托管件：" + (buildScriptDir / "Lemon.Entry.dll").string());
        return 1;
    }
    lemon::assets::AssetIndex index;
    if (!index.Open(project.string(), kSpriteIdBase)) {
        Err("资产索引不可用（无 Assets/ 目录？）：" + project.string());
        return 1;
    }
    { // guid 冲突（同 guid 双文件 = 运行时 FindByGuid 静默取先者——打包期响亮）
        std::unordered_set<uint64_t> seen;
        for (const lemon::assets::IndexedEntry& e : index.Entries())
            if (!seen.insert(e.guid).second)
                Err("guid 冲突：" + e.relPath + "（" +
                    lemon::assets::GuidToHex(e.guid) + " 已被先前条目占用）");
    }
    CheckDanglingRefs(project, index);
    std::printf("[lemon-packager] 校验过：entryScene=%s 资产 %u 条%s\n", entryScene.c_str(),
                (unsigned)index.Entries().size(), g_errors ? "（含红字，见上）" : "");
    if (g_errors) return 1;

    // ---- 2. 出包根 ----
    if (fs::exists(out, ec)) {
        if (!force) {
            Err("出包根已存在（--force 整删重建）：" + out.string());
            return 1;
        }
        fs::remove_all(out, ec);
    }
    fs::create_directories(out, ec);

    // ---- 3. 二进制 + dylib 闭包（otool 递归收 → 拷贝 → 重锚 → 签名）----
    CopyFileTo(gameExe, out / "lemon-game");
    fs::path moltenVk;
    for (const fs::path& c : {fs::path("/usr/local/opt/molten-vk/lib/libMoltenVK.dylib"),
                              fs::path("/usr/local/lib/libMoltenVK.dylib")})
        if (fs::is_regular_file(c, ec)) {
            moltenVk = c;
            break;
        }
    // 闭包收件：引用名（包内文件名 = 引用串 basename）→ 本体路径
    std::unordered_map<std::string, fs::path> bundle;
    std::vector<fs::path> work{out / "lemon-game"};
    if (!moltenVk.empty()) bundle["libMoltenVK.dylib"] = moltenVk;
    while (!work.empty()) {
        const fs::path macho = work.back();
        work.pop_back();
        for (const std::string& ref : OtoolDeps(macho)) {
            if (IsSystemDep(ref) || ref.rfind("@executable_path", 0) == 0) continue;
            const std::string name = BaseName(ref);
            if (name.empty() || bundle.count(name)) continue;
            const fs::path real = ResolveRef(ref, out, buildEntryDir);
            if (real.empty()) {
                Err("依赖不可解析：" + ref + "（被 " + macho.filename().string() + " 引用）");
                continue;
            }
            // install-name 自引用跳过（otool -L 第二行是本体 id 非——freetype 的
            // id 名 6.dylib ≠ 引用名，曾致双收;其余库同名被去重掩盖）
            std::error_code cec;
            if (fs::weakly_canonical(real, cec) == fs::weakly_canonical(macho, cec)) continue;
            bundle[name] = real;
            work.push_back(real); // 递归：本体自身的 brew 依赖（freetype→libpng 链）
        }
    }
    if (g_errors) return 1;
    for (const auto& [name, real] : bundle) CopyFileTo(real, out / name);
    // 重锚：本体 id → @rpath/<name>；本体间依赖 → @rpath/<name>；lemon-game 的绝对
    // 引用 → @rpath/<name>（@rpath 引用无需改写——pkg 根 rpath 命中同名件）
    for (const auto& [name, real] : bundle) {
        for (const std::string& dep : OtoolDeps(out / name)) {
            if (IsSystemDep(dep)) continue;
            const std::string depName = BaseName(dep);
            if (bundle.count(depName) && dep != "@rpath/" + depName)
                RunOk("install_name_tool -change " + Quote(dep) + " '@rpath/" + depName + "' " +
                          Quote(out / name),
                      "重锚 " + name + " 依赖 " + depName);
        }
        RunOk("install_name_tool -id '@rpath/" + name + "' " + Quote(out / name),
              "改 id " + name);
    }
    for (const std::string& dep : OtoolDeps(out / "lemon-game")) {
        if (IsSystemDep(dep)) continue;
        const std::string depName = BaseName(dep);
        if (bundle.count(depName) && dep != "@rpath/" + depName)
            RunOk("install_name_tool -change " + Quote(dep) + " '@rpath/" + depName + "' " +
                      Quote(out / "lemon-game"),
                    "重锚 lemon-game 依赖 " + depName);
    }
    Run("install_name_tool -add_rpath '@loader_path' " + Quote(out / "lemon-game")); // 已有则忽略失败
    // rpath 卫生：非 @loader_path 的 rpath 全清（构建机残留路径会按序遮蔽包内件——
    // JDK rpath 遮蔽 freetype 的实抓事故；@rpath 依赖统一经 exe 的 @loader_path 命中）
    for (const std::string& rp : RpathsOf(out / "lemon-game"))
        if (rp != "@loader_path")
            RunOk("install_name_tool -delete_rpath " + Quote(rp) + " " + Quote(out / "lemon-game"),
                  "删残留 rpath " + rp);
    for (const auto& [name, real] : bundle)
        for (const std::string& rp : RpathsOf(out / name))
            if (rp != "@loader_path")
                RunOk("install_name_tool -delete_rpath " + Quote(rp) + " " + Quote(out / name),
                      "删残留 rpath " + rp + "（" + name + "）");
    // ad-hoc 重签（install_name_tool 改动即失效签名；arm64 干净机 dyld 硬性要求）
    RunOk("codesign -f -s - " + Quote(out / "lemon-game"), "签名 lemon-game");
    for (const auto& [name, real] : bundle)
        RunOk("codesign -f -s - " + Quote(out / name), "签名 " + name);
    // ICD 清单（api_version 抄系统注册位；相对 library_path 随包可搬迁）
    if (!moltenVk.empty()) {
        std::string apiVer = "1.4.0";
        std::ifstream sj("/usr/local/etc/vulkan/icd.d/MoltenVK_icd.json");
        if (sj) {
            std::string t((std::istreambuf_iterator<char>(sj)), std::istreambuf_iterator<char>());
            const Json j = Json::parse(t, nullptr, false);
            if (!j.is_discarded() && j.contains("ICD") && j["ICD"].contains("api_version"))
                apiVer = j["ICD"]["api_version"].get<std::string>();
        }
        std::ofstream icd(out / "MoltenVK_icd.json", std::ios::trunc);
        icd << "{\n  \"file_format_version\": \"1.0.0\",\n  \"ICD\": {\n"
            << "    \"library_path\": \"libMoltenVK.dylib\",\n"
            << "    \"api_version\": \"" << apiVer << "\",\n"
            << "    \"is_portability_driver\": true\n  }\n}\n";
        Record(out / "MoltenVK_icd.json");
    }
    std::printf("[lemon-packager] 闭包：dylib %u 件%s\n", (unsigned)bundle.size(),
                moltenVk.empty() ? "（MoltenVK 未收——干净机将无 ICD）" : " + MoltenVK ICD");
    if (g_errors) return 1;

    // ---- 4. runtime/（dotnet publish self-contained；类库拒自含 → 合成宿主工程）----
    const fs::path hostDir = fs::temp_directory_path(ec) /
                             ("lemon-pkg-host-" + std::to_string(::getpid()));
    fs::create_directories(hostDir, ec);
    {
        std::ofstream cs(hostDir / "Host.csproj", std::ios::trunc);
        cs << "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
           << "  <!-- lemon-packager 合成宿主（M7a 批⑤）：Game.csproj 是类库，dotnet "
              "publish 的 self-contained 开关拒非 Exe 工程。宿主只为携带 runtime 树与"
              "单源解析（ProjectReference 指 Game、Reference 指 Lemon.Entry 构建树"
              "产物）。宿主产物三件随后剪除，native 侧入口仍是 Lemon.Entry。 -->\n"
           << "  <PropertyGroup>\n"
           << "    <TargetFramework>net10.0</TargetFramework>\n"
           << "    <OutputType>Exe</OutputType>\n"
           << "    <AssemblyName>Host</AssemblyName>\n"
           << "    <Nullable>disable</Nullable>\n"
           << "  </PropertyGroup>\n"
           << "  <ItemGroup>\n"
           << "    <ProjectReference Include=\"" << (project / "Game" / "Game.csproj").string()
           << "\" />\n"
           << "    <Reference Include=\"Lemon.Entry\">\n"
           << "      <HintPath>" << (buildScriptDir / "Lemon.Entry.dll").string()
           << "</HintPath>\n"
           << "    </Reference>\n"
           << "  </ItemGroup>\n"
           << "</Project>\n";
        std::ofstream pm(hostDir / "Program.cs", std::ios::trunc);
        pm << "internal static class Program { static void Main() {} }\n";
    }
    const CmdResult pub = Run("dotnet publish " + Quote(hostDir / "Host.csproj") +
                              " -c Release -r osx-x64 --self-contained -o " + Quote(out / "runtime"));
    if (pub.rc != 0) {
        Err("dotnet publish 失败（输出见上）");
        std::printf("%s", pub.out.c_str());
        return 1;
    }
    fs::remove_all(hostDir, ec);
    const fs::path rt = out / "runtime";
    if (!fs::is_regular_file(rt / "Host.runtimeconfig.json", ec) ||
        !fs::is_regular_file(rt / "Lemon.Entry.dll", ec) ||
        !fs::is_regular_file(rt / "Game.dll", ec)) {
        Err("publish 产物缺关键件（Host.runtimeconfig.json/Lemon.Entry.dll/Game.dll）");
        return 1;
    }
    // 自含式 runtimeconfig 落 GameEntry 消费名（hostfxr 以 config 所在目录为 app
    // base 找 hostpolicy——平铺产物即自含布局；CoreCLRHost 批⑤ 平铺形态命中）
    CopyFileTo(rt / "Host.runtimeconfig.json", rt / "Lemon.Entry.runtimeconfig.json");
    for (const char* prune : {"Host", "Host.dll", "Host.pdb", "Host.deps.json"})
        fs::remove(rt / prune, ec); // 宿主可执行/产物剪除（native 入口仍是 Lemon.Entry）
    std::printf("[lemon-packager] runtime/：dotnet publish self-contained（osx-x64）落位\n");

    // ---- 5. data/（项目直拷 + 预生成件）----
    const fs::path data = out / "data";
    CopyTree(project, data, {".lemon", ".git"});
    if (g_errors) return 1;
    fs::create_directories(data / ".lemon" / "bin", ec);
    // Game.deps.json 宽容拷贝：publish 树里 deps 记账在宿主名下（已剪除）——
    // Game.dll 依赖走 app base 平铺探测（Lemon.SDK 同目录），不消费其 deps.json
    for (const char* f : {"Game.dll", "Game.deps.json", "Lemon.SDK.dll"}) {
        std::error_code lec;
        if (fs::is_regular_file(rt / f, lec)) CopyFileTo(rt / f, data / ".lemon" / "bin" / f);
        else if (std::strcmp(f, "Game.deps.json") != 0) {
            Err(std::string("publish 产物缺 ") + f + "（runtime/ 内应有）");
            return 1;
        }
    }
    // 音频现烤（AudioMount 同路径 <root>/.lemon/baked/audio/<guidhex>.baked——
    // mtime 新鲜则运行时零分支;loop 参数自 .meta importer（AssetIndex 读入面））
    uint32_t baked = 0;
    fs::create_directories(data / ".lemon" / "baked" / "audio", ec);
    for (const lemon::assets::IndexedEntry& e : index.Entries()) {
        if (e.type != lemon::assets::AssetType::Audio) continue;
        char hex[17];
        std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)e.guid);
        const fs::path dst = data / ".lemon" / "baked" / "audio" / (std::string(hex) + ".baked");
        if (lemon::audio::BakeAudioFile(index.AbsolutePath(e).c_str(), dst.string().c_str(),
                                        e.audioLoopStart, e.audioLoopEnd)) {
            Record(dst);
            ++baked;
        } else
            Err("音频烤制失败：" + e.relPath);
    }
    if (!index.ExportManifest((data / ".lemon" / "manifest.pkg.json").string()))
        Err("manifest.pkg.json 导出失败");
    else
        Record(data / ".lemon" / "manifest.pkg.json");
#ifndef LEMON_ENGINE_FONT_DIR
    Err("lemon-packager 未携带 LEMON_ENGINE_FONT_DIR（构建配置缺失）");
    return 1;
#else
    fs::create_directories(data / "Fonts", ec);
    CopyFileTo(fs::path(LEMON_ENGINE_FONT_DIR) / "NotoSansSC-Regular.otf",
               data / "Fonts" / "NotoSansSC-Regular.otf");
    CopyFileTo(fs::path(LEMON_ENGINE_FONT_DIR) / "OFL.txt", data / "Fonts" / "OFL.txt"); // OFL 随包义务
#endif
    std::printf("[lemon-packager] data/：直拷 + Game.dll + 烤制 %u + manifest.pkg.json + 字体\n",
                baked);
    if (g_errors) return 1;

    // ---- 6. 自检（otool 闭环 + 关键件 + 清单对账）----
    uint32_t closureMiss = 0;
    std::vector<fs::path> machos{out / "lemon-game"};
    for (const auto& [name, real] : bundle) machos.push_back(out / name);
    for (const fs::path& m : machos) {
        for (const std::string& dep : OtoolDeps(m)) {
            if (IsSystemDep(dep)) continue;
            if (dep.rfind("@rpath/", 0) != 0 || !fs::is_regular_file(out / BaseName(dep), ec)) {
                Err("闭包缺口：" + m.filename().string() + " -> " + dep);
                ++closureMiss;
            }
        }
        // rpath 卫生自检：非 @loader_path 残留 = 构建机路径泄漏（遮蔽风险）
        for (const std::string& rp : RpathsOf(m))
            if (rp != "@loader_path") {
                Err("rpath 残留：" + m.filename().string() + " -> " + rp);
                ++closureMiss;
            }
    }
    uint32_t essentialMiss = 0;
    for (const fs::path& need :
         {rt / "libhostfxr.dylib", rt / "libhostpolicy.dylib", rt / "Lemon.Entry.dll",
          rt / "Lemon.Entry.runtimeconfig.json", rt / "Game.dll", rt / "Lemon.SDK.dll",
          data / "project.lemon", data / entryScene, data / ".lemon" / "bin" / "Game.dll",
          data / ".lemon" / "manifest.pkg.json", data / "Fonts" / "NotoSansSC-Regular.otf",
          out / "MoltenVK_icd.json", out / "libMoltenVK.dylib"})
        if (!fs::is_regular_file(need, ec)) {
            Err("关键件缺失：" + fs::relative(need, out, ec).string());
            ++essentialMiss;
        }
    // 清单对账：记账 ⊆ 实走；publish 树（runtime/）外零多出
    uint32_t inventoryMiss = 0, extra = 0;
    std::set<std::string> actual;
    uint64_t bytes = 0;
    for (auto it = fs::recursive_directory_iterator(out, ec); it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        const std::string p = fs::absolute(it->path()).generic_string();
        actual.insert(p);
        bytes += (uint64_t)it->file_size(ec);
    }
    for (const std::string& p : g_written)
        if (!actual.count(p)) {
            Err("记账文件不在包内：" + p);
            ++inventoryMiss;
        }
    for (const std::string& p : actual) {
        const std::string rel = p.substr(out.generic_string().size());
        if (rel.rfind("/runtime", 0) == 0 || rel.find("/runtime/") == 0) continue;
        if (!g_written.count(p)) {
            Err("包内多出未记账文件：" + p);
            ++extra;
        }
    }
    const bool ok = g_errors == 0;
    std::printf(
        "[lemon-packager] RESULT pkg-selfcheck: files=%llu bytes=%lluMiB dylibs=%u baked=%u "
        "manifest=%u closureMiss=%u essentialMiss=%u inventoryMiss=%u extra=%u => %s\n",
        (unsigned long long)actual.size(), bytes >> 20, (unsigned)bundle.size(), baked,
        (unsigned)index.Entries().size(), closureMiss, essentialMiss, inventoryMiss, extra,
        ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}
