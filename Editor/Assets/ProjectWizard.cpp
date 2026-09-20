// Lemon 编辑器 — 项目向导实现（blank 模板；06 §1/§7）
// stb_image_write 只经 Tooling/StbImpl.cpp 的唯一定义 TU（隔离纪律同 AssetGpuCache）。
#include "Assets/ProjectWizard.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

std::string ProjectWizard::Create(const ProjectDesc& d, uint64_t* outSpawnGuid) {
    if (d.name.empty() || d.parentDir.empty() || d.sdkDir.empty()) return {};
    std::error_code ec;
    fs::path root = fs::path(d.parentDir) / d.name;
    if (fs::exists(root, ec)) {
        LEMON_WARN("新建项目失败：目录已存在 %s", root.string().c_str());
        return {};
    }
    for (const char* dir : {"Assets", "Scenes", "Prefabs", "Game", "Data", ".lemon/editor",
                            "Builds"})
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
                                    double* outSeconds) {
    const auto t0 = std::chrono::steady_clock::now();
    std::error_code ec;
    fs::create_directories(outDir, ec);
    // 外置 dotnet CLI（04 §6：M4 用外置；内嵌 Roslyn 后置）。静默常规输出，
    // 失败时 stderr 直通（Console 收到的是非零返回码红字）。
    std::string cmd = "dotnet build \"" + csprojAbs + "\" -c Release --nologo -v q "
                      "-o \"" + outDir + "\" 2>&1 >/dev/null";
    const int rc = std::system(cmd.c_str());
    if (outSeconds)
        *outSeconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                t0)
                          .count();
    return rc == 0 ? 0 : (rc == -1 ? -1 : 1);
}

} // namespace lemon::editor
