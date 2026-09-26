// Lemon 编辑器 — 项目向导实现（blank 模板；06 §1/§7）
// stb_image_write 只经 Tooling/StbImpl.cpp 的唯一定义 TU（隔离纪律同 AssetGpuCache）。
#include "Assets/ProjectWizard.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "stb_image_write.h"

#include "Assets/AssetDatabase.h"
#include "Core/Guid.h"
#include "Core/Log.h"
#include "ECS/Scene.h"
#include "Serialization/SceneArchive.h"

namespace lemon::editor {
namespace fs = std::filesystem;
using ecs::SceneArchive;

namespace {
// Game/*.csproj 的 Lemon.SDK HintPath 重锚（M5 批④ 模板分支）：正则不动，
// 逐 .csproj 找 <HintPath>...</HintPath>（Lemon.SDK.dll 结尾）整行替换。
// 无 csproj/无锚点 = 告警一次不阻断（模板仍可手改）。
void ReanchorSdkHintPath(const fs::path& gameDir, const std::string& sdkDir) {
    std::error_code ec;
    if (!fs::is_directory(gameDir, ec)) return;
    const std::string want = "<HintPath>" + (fs::path(sdkDir) / "Lemon.SDK.dll").string() +
                             "</HintPath>";
    for (auto it = fs::directory_iterator(gameDir, ec); it != fs::directory_iterator();
         it.increment(ec)) {
        if (ec || !it->is_regular_file(ec) || it->path().extension() != ".csproj") continue;
        const fs::path csproj = it->path();
        std::ifstream in(csproj, std::ios::binary);
        std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        const size_t at = src.find("<HintPath>");
        if (at == std::string::npos) continue;
        const size_t end = src.find("</HintPath>", at);
        if (end == std::string::npos) continue;
        const size_t after = end + std::strlen("</HintPath>");
        if (src.compare(at, after - at, want) != 0) {
            src.replace(at, after - at, want);
            std::ofstream out(csproj, std::ios::trunc);
            out << src;
            LEMON_LOG("模板 csproj SDK 已重锚：%s", csproj.filename().string().c_str());
        }
        return; // 单脚本工程：首个 csproj 即工程文件（已锚定 = 本机生成路径一致）
    }
    LEMON_WARN("模板 Game/ 无 .csproj 或无 HintPath 锚点（SDK 引用需手改）");
}

} // namespace

/// 项目名 = 单段目录名（2026-09-24 审查 F-02）：拒绝绝对路径、路径分隔符与
/// "."/".." 逃逸段——名字直接拼进父目录，"../x" 会使创建根越出父目录。
/// 其余字符（含中文/空格）交给文件系统，保持向导可用性。
bool IsValidProjectName(const std::string& n) {
    if (n.empty() || n.size() > 63) return false;
    if (n == "." || n == "..") return false;
    for (char c : n)
        if (c == '/' || c == '\\' || c == ':' || (unsigned char)c < 0x20) return false;
    return fs::path(n).parent_path().empty();
}

std::string ProjectWizard::Create(const ProjectDesc& d, uint64_t* outSpawnGuid) {
    if (d.name.empty() || d.parentDir.empty()) return {};
    if (!IsValidProjectName(d.name)) {
        LEMON_WARN("新建项目失败：项目名不是合法目录名（禁止路径分隔符/: 与 ..）：%s",
                   d.name.c_str());
        return {};
    }
    std::error_code ec;
    // 父目录绝对化（手敲 ./x 等相对路径）：返回 root 与全链路径保持绝对——
    // LoadFromAssemblyPath 只收绝对路径（M4.6 实测闪退根因之一）
    fs::path root = fs::absolute(d.parentDir, ec) / d.name;
    if (fs::exists(root, ec)) {
        LEMON_WARN("新建项目失败：目录已存在 %s", root.string().c_str());
        return {};
    }

    // ---- M5 批④：vs-survivor 模板分支（06 §1 "选模板 → 复制模板"流程）----
    // 拷贝整棵模板项目（资产 .meta 随行 = GUID/切片稳定）→ 重写 project.lemon
    // （新项目 GUID = 存档隔离键；模板资产 GUID 不重生成——引用锚点）→ 重锚
    // Game/*.csproj HintPath（创建期固化，blank 同款纪律）。
    const bool isTemplate = d.templateName != "blank" && !d.templateName.empty();
    if (isTemplate) {
        fs::path src = d.templateDir.empty() ? fs::path("Templates") / d.templateName
                                             : fs::path(d.templateDir);
        if (src.is_relative()) src = fs::absolute(src, ec);
        if (!fs::is_directory(src / "Assets") || !fs::is_regular_file(src / "project.lemon")) {
            LEMON_WARN("模板缺失或不完整：%s", src.string().c_str());
            return {};
        }
        fs::create_directories(root.parent_path(), ec); // 中间目录（父目录可能未建）
        fs::copy(src, root, fs::copy_options::recursive, ec);
        if (ec) {
            LEMON_WARN("模板拷贝失败：%s", ec.message().c_str());
            fs::remove_all(root, ec);
            return {};
        }
        // project.lemon 重写：名字/引擎版本/新项目 GUID（模板占位档替换）
        const uint64_t projectGuid = GenerateGuid();
        {
            std::ofstream f(root / "project.lemon", std::ios::trunc);
            f << "{\n  \"schemaVersion\": 1,\n  \"name\": \"" << d.name << "\",\n"
              << "  \"engineVersion\": \"" << d.engineVersion << "\",\n"
              << "  \"guid\": \"" << AssetDatabase::GuidToHex(projectGuid) << "\"\n}\n";
        }
        // Game/*.csproj HintPath 重锚（找到 Reference Include="Lemon.SDK" 段替换）
        if (!d.sdkDir.empty()) ReanchorSdkHintPath(root / "Game", d.sdkDir);
        LEMON_LOG("项目已创建（%s 模板）：%s（project guid %016llx）",
                  d.templateName.c_str(), root.string().c_str(),
                  (unsigned long long)projectGuid);
        return root.string();
    }

    // 目录骨架（06 §1）。T3b-9：Assets/anims、Assets/sprites = 推荐分类落点
    //（约定不强制——用户可自由重组/删除；guid 随 .meta 走，引用不受目录影响）
    for (const char* dir : {"Assets", "Assets/anims", "Assets/sprites", "Scenes", "Prefabs",
                            "Game", "Data", ".lemon/editor", "Builds"})
        fs::create_directories(root / dir, ec);

    // project.lemon：引擎版本锚点（06 §1：启动校验/迁移提示的依据）
    const uint64_t projectGuid = GenerateGuid();
    {
        std::ofstream f(root / "project.lemon", std::ios::trunc);
        f << "{\n  \"schemaVersion\": 1,\n  \"name\": \"" << d.name << "\",\n"
          << "  \"engineVersion\": \"" << d.engineVersion << "\",\n"
          << "  \"guid\": \"" << AssetDatabase::GuidToHex(projectGuid) << "\"\n}\n";
    }
    // 项目 .gitignore（bin/obj = dotnet 噪声；.lemon/Builds = 状态与出包）
    {
        std::ofstream f(root / ".gitignore", std::ios::trunc);
        f << "bin/\nobj/\n.lemon/\nBuilds/\n.DS_Store\n";
    }

    // 种子资产：Assets/spawn.png（32×32 柠檬黄圆形 + 深色描边）+ 固定 guid .meta
    const uint64_t spawnGuid = GenerateGuid();
    {
        std::vector<uint8_t> px(32 * 32 * 4);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const float dx = x - 15.5f, dy = y - 15.5f;
                const float r = dx * dx + dy * dy;
                uint8_t* q = &px[((size_t)y * 32 + x) * 4];
                if (r > 15.0f * 15.0f) {
                    q[0] = 0; q[1] = 0; q[2] = 0; q[3] = 0; // 圆外透明
                } else if (r > 13.0f * 13.0f) {
                    q[0] = 90; q[1] = 74; q[2] = 12; q[3] = 255; // 描边
                } else {
                    q[0] = 250; q[1] = 212; q[2] = 30; q[3] = 255; // 柠檬黄
                }
            }
        stbi_write_png((root / "Assets" / "spawn.png").string().c_str(), 32, 32, 4, px.data(),
                       32 * 4);
        std::ofstream m(root / "Assets" / "spawn.png.meta", std::ios::trunc);
        m << "{\n  \"guid\": \"" << AssetDatabase::GuidToHex(spawnGuid)
          << "\",\n  \"type\": \"sprite\",\n  \"hash\": 0,\n  \"importedAt\": 0\n}\n";
    }

    if (!WriteGameProject(d, spawnGuid)) return {};

    // Scenes/Main.scene：空场景起步（EditorContext::OpenScene 同一解码器）
    {
        ecs::Scene scene("Main");
        std::ofstream f(root / "Scenes" / "Main.scene", std::ios::trunc);
        f << SceneArchive::Save(scene);
    }
    if (outSpawnGuid) *outSpawnGuid = spawnGuid;
    LEMON_LOG("项目已创建（blank 模板）：%s（spawn guid %016llx）", root.string().c_str(),
              (unsigned long long)spawnGuid);
    return root.string();
}

bool ProjectWizard::WriteGameProject(const ProjectDesc& d, uint64_t spawnGuid) {
    if (d.sdkDir.empty()) return false;
    fs::path game = fs::path(d.parentDir) / d.name / "Game";
    // HintPath 绝对锚定（创建期固化；06 §1 安装模型 M5 随安装器迁相对锚）
    fs::path sdk(d.sdkDir);
    {
        std::ofstream f(game / (d.name + ".csproj"), std::ios::trunc);
        f << "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
          << "  <!-- Lemon blank 模板脚本工程：引用随引擎分发的 Lemon.SDK -->\n"
          << "  <PropertyGroup>\n"
          << "    <TargetFramework>net10.0</TargetFramework>\n"
          << "    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n"
          << "    <Nullable>enable</Nullable>\n"
          << "    <AssemblyName>" << d.name << "</AssemblyName>\n"
          << "    <RootNamespace>" << d.name << "</RootNamespace>\n"
          << "    <GenerateRuntimeConfigurationFiles>false</GenerateRuntimeConfigurationFiles>\n"
          << "  </PropertyGroup>\n"
          << "  <ItemGroup>\n"
          << "    <Reference Include=\"Lemon.SDK\">\n"
          << "      <HintPath>" << (sdk / "Lemon.SDK.dll").string() << "</HintPath>\n"
          << "    </Reference>\n"
          << "  </ItemGroup>\n"
          << "</Project>\n";
    }
    {
        std::ofstream f(game / "GameMain.cs", std::ios::trunc);
        f << "using Lemon;\n\n"
          << "public static class GameMain\n"
          << "{\n"
          << "    public static void Configure()\n"
          << "    {\n"
          << "        Lemon.Behaviours.Register<InputMoverBehaviour>();\n"
          << "        Lemon.Behaviours.Register<SpawnerBehaviour>();\n"
          << "    }\n"
          << "}\n";
    }
    {
        std::ofstream f(game / "InputMoverBehaviour.cs", std::ios::trunc);
        f << "using Lemon;\n\n"
          << "/// <summary>方向键/WASD 移动（GameView 聚焦时输入进游戏）。</summary>\n"
          << "public sealed class InputMoverBehaviour : LemonBehaviour\n"
          << "{\n"
          << "    public const float Speed = 240f;\n\n"
          << "    protected override void Update()\n"
          << "    {\n"
          << "        var t = gameObject.GetComponent<Lemon.Interop.Transform2D>();\n"
          << "        t.Pos = t.Pos + Lemon.Input.Axis * (Speed / 60f);\n"
          << "        gameObject.SetComponent(t);\n"
          << "    }\n"
          << "}\n";
    }
    {
        std::ofstream f(game / "SpawnerBehaviour.cs", std::ios::trunc);
        f << "using Lemon;\n\n"
          << "/// <summary>在自身位置周期刷怪（spawn.png 种子资产；StateBag 热重载示例）。</summary>\n"
          << "public sealed class SpawnerBehaviour : LemonBehaviour\n"
          << "{\n"
          << "    private const string kSpriteGuid = \"" << AssetDatabase::GuidToHex(spawnGuid)
          << "\";\n"
          << "    private int _tick;\n"
          << "    private uint _spriteId;\n\n"
          << "    protected override void Start()\n"
          << "        => _spriteId = Lemon.Assets.SpriteOf(kSpriteGuid);\n\n"
          << "    protected override void Update()\n"
          << "    {\n"
          << "        if (++_tick < 5 || _tick > 40) return;\n"
          << "        var t = gameObject.GetComponent<Lemon.Interop.Transform2D>();\n"
          << "        Lemon.Instantiate.Spawn(_spriteId,\n"
          << "            new Lemon.Vec2(t.Pos.X + 40 + _tick * 2, t.Pos.Y - 20));\n"
          << "    }\n\n"
          << "    protected override void OnHotReloadOut(StateBag bag)\n"
          << "    {\n"
          << "        bag.Set(\"tick\", _tick);\n"
          << "        bag.Set(\"spriteId\", _spriteId);\n"
          << "    }\n\n"
          << "    protected override void OnHotReloadIn(StateBag bag)\n"
          << "    {\n"
          << "        if (bag.TryGet(\"tick\", out int t)) _tick = t;\n"
          << "        if (bag.TryGet(\"spriteId\", out uint s)) _spriteId = s;\n"
          << "    }\n"
          << "}\n";
    }
    return true;
}

int ProjectWizard::BuildGameProject(const std::string& csprojAbs, const std::string& outDir,
                                    double* outSeconds, std::string* outOutput) {
    const auto t0 = std::chrono::steady_clock::now();
    std::error_code ec;
    fs::create_directories(outDir, ec);
    // 外置 dotnet CLI（04 §6：M4 用外置；内嵌 Roslyn 后置）。M4.6 起捕获合并输出
    // （编译错误行是 Console 解析原料；-v q 只剩错误/警告，量小）。
    std::string cmd = "dotnet build \"" + csprojAbs + "\" -c Release --nologo -v q -o \"" +
                      outDir + "\" 2>&1";
    int rc = -1;
    if (FILE* pipe = popen(cmd.c_str(), "r")) {
        std::string out;
        char buf[4096];
        size_t n = 0;
        while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) {
            if (out.size() < 64 * 1024) out.append(buf, n); // 上限保护（错误洪泛不进环）
        }
        const int status = pclose(pipe);
        rc = status >= 0 ? status : -1;
        if (outOutput) *outOutput = std::move(out);
    }
    if (outSeconds)
        *outSeconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                t0)
                          .count();
    return rc == 0 ? 0 : (rc == -1 ? -1 : 1);
}

std::vector<std::string> ProjectWizard::ExtractCompileErrors(const std::string& dotnetOutput) {
    // dotnet/MSBuild 错误行形如：
    //   /abs/path/Game/A.cs(13,31): error CS1002: ; expected [/abs/path/Game/x.csproj]
    // 提取含 ": error CS" 的行、去 " [....csproj]" 尾巴；上限 50 行（Console 环容量考虑）
    std::vector<std::string> out;
    size_t at = 0;
    while (out.size() < 50) {
        const size_t eol = dotnetOutput.find('\n', at);
        std::string line = dotnetOutput.substr(
            at, (eol == std::string::npos ? dotnetOutput.size() : eol) - at);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.find(": error CS") != std::string::npos) {
            if (const size_t br = line.rfind(" ["); br != std::string::npos &&
                                                  line.back() == ']' &&
                                                  line.find(".csproj]", br) != std::string::npos)
                line.resize(br);
            // dotnet 失败时把每条错误打两遍（编译段 + 失败摘要段）——同文去重，
            // 否则 Console 红字整段翻倍（2026-09-22 测试报告观察 3 实测定位）
            if (std::find(out.begin(), out.end(), line) == out.end())
                out.push_back(std::move(line));
        }
        if (eol == std::string::npos) break;
        at = eol + 1;
    }
    return out;
}

bool ProjectWizard::AddBehaviourScript(const std::string& gameDirAbs,
                                       const std::string& className) {    // 类名 = C# 标识符（首字符字母/下划线；限长防 tag 类缓冲溢出类问题）
    if (className.empty() || className.size() > 48) return false;
    if (!std::isalpha((unsigned char)className[0]) && className[0] != '_') return false;
    for (char c : className)
        if (!std::isalnum((unsigned char)c) && c != '_') return false;

    fs::path game(gameDirAbs);
    std::error_code ec;
    const fs::path cs = game / (className + ".cs");
    if (fs::exists(cs, ec)) {
        LEMON_WARN("新建脚本失败：已存在 %s", cs.string().c_str());
        return false;
    }
    {
        std::ofstream f(cs, std::ios::trunc);
        f << "using Lemon;\n\n"
          << "/// <summary>编辑器新建脚本（M4.6 模板）。</summary>\n"
          << "public sealed class " << className << " : LemonBehaviour\n"
          << "{\n"
          << "    protected override void Start() { }\n\n"
          << "    protected override void Update() { }\n"
          << "}\n";
    }
    // 注册行（类型可挂的前提）：锚定首个 Register 行前插（与终验同款锚点手法，
    // 不动 Configure 签名 → 大括号配对无险）。无锚点（用户改过 GameMain）= 告警手注册。
    const fs::path main = game / "GameMain.cs";
    std::ifstream in(main, std::ios::binary);
    std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string regLine = "Lemon.Behaviours.Register<" + className + ">();";
    if (src.find(regLine) == std::string::npos) {
        const std::string anchors[] = {"Lemon.Behaviours.Register<InputMoverBehaviour>();",
                                       "Lemon.Behaviours.Register<"};
        for (const std::string& a : anchors) {
            const size_t at = src.find(a);
            if (at != std::string::npos) {
                src.insert(at, regLine + "\n        ");
                std::ofstream out(main, std::ios::trunc);
                out << src;
                return true;
            }
        }
        LEMON_WARN("脚本已建但未能自动注册（GameMain.cs 无注册锚点，请手加 %s）",
                   regLine.c_str());
    }
    return true;
}

} // namespace lemon::editor
