// Lemon 编辑器 — 资产数据库（M4-Editor-Plan §5 M4.4；06 §2 GUID/.meta/manifest）
// 纯文件系统 + JSON 逻辑，零 GPU/ImGui 依赖（可单测；GPU 侧见 AssetGpuCache）。
//   * GUID 稳定键：.meta 随文件走（重命名/移动引用不断）；.scene 只存 GUID/spriteId。
//   * spriteId 持久分配（manifest 记账，只增不减）：已存场景引用不因增删资产漂移。
//   * 删除文件 = 墓碑（missing；号保留），重启不回收——编号连续性是
//     "AtlasRegistry 追加式 id ↔ DB 分配号"对齐的前提。
//   * 启动/重扫体检（孤儿 meta / GUID 冲突 / 墓碑引用）红字进 Console。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lemon::editor {

enum class AssetType : uint8_t { Sprite, Prefab, Script, Generic };

const char* AssetTypeName(AssetType t);
struct AssetEntry {
    uint64_t guid = 0;
    std::string relPath;   // 相对 Assets/（'/' 分隔，含扩展名）
    AssetType type = AssetType::Generic;
    uint32_t spriteId = 0; // Sprite：AtlasRegistry 稳定 id（0 = 非 sprite）
    uint64_t hash = 0;     // 内容 FNV-1a 64（重导入判定）
    bool missing = false;  // 墓碑（文件已删；号保留，浏览器隐藏）

    std::string FileName() const; // relPath 末段
    std::string Dir() const;      // relPath 去末段（"" = 根；无尾 '/'）
};

class AssetDatabase {
public:
    struct ChangeSet {
        std::vector<uint64_t> added, modified, removed;
        bool Empty() const { return added.empty() && modified.empty() && removed.empty(); }
    };

    /// 打开项目（root 含 Assets/；缺则建空目录）。spriteIdBase = 程序化图集之后
    /// 首个可用 id（调用方 = ViewportRenderer 装配后 SpriteCount()+1）。
    /// 扫描 + meta 补齐 + manifest 载入/落盘 + 体检。返回 false = root 不可写。
    bool OpenProject(const std::string& projectRoot, uint32_t spriteIdBase);

    /// 重扫（FileWatcher 触发/手动）：保 guid/spriteId；产出 LastChange。
    void Rescan();

    const ChangeSet& LastChange() const { return lastChange_; }

    // ---- 查询（含墓碑；调用方按需过滤 missing）----
    const AssetEntry* FindByGuid(uint64_t guid) const;
    const AssetEntry* FindByPath(const std::string& relPath) const;
    const AssetEntry* FindBySpriteId(uint32_t spriteId) const;
    /// 可变版（编辑器操作 Rename/Remove 用；DB 持有者 = EditorContext）
    AssetEntry* FindByGuid(uint64_t guid) {
        return const_cast<AssetEntry*>(std::as_const(*this).FindByGuid(guid));
    }
    AssetEntry* FindByPath(const std::string& relPath) {
        return const_cast<AssetEntry*>(std::as_const(*this).FindByPath(relPath));
    }
    const std::vector<AssetEntry>& Entries() const { return entries_; }
    /// 指定目录直下条目（dirPrefix "" = 根；含子目录占位由 UI 自理）
    std::vector<const AssetEntry*> EntriesInDir(const std::string& dirPrefix) const;
    /// 全部目录表（排序；"" 恒在首位）
    std::vector<std::string> Directories() const;
    uint32_t SpriteAssetCount() const;

    const std::string& ProjectRoot() const { return root_; }
    std::string AssetsRoot() const;
    std::string AbsolutePath(const AssetEntry& e) const { return AssetsRoot() + "/" + e.relPath; }
    /// 16 位 hex（Inspector 槽显示 / C# Assets.SpriteOf 参数形态）
    static std::string GuidToHex(uint64_t guid);
    static uint64_t HexToGuid(const char* hex);

    // ---- 编辑器操作（同步落盘；guid 稳定 → 场景引用不断）----
    /// 重命名/移动（相对 Assets/ 的新路径）。失败（目标存在/IO 错）false。
    bool Rename(AssetEntry& e, const std::string& newRelPath);
    /// 删除（文件 + .meta；条目转墓碑）。undo 层面由调用方抓场景快照。
    bool Remove(AssetEntry& e);
    /// 导入外部文件（复制进 Assets/ 下 relDest）；返回新条目（失败 nullptr）。
    const AssetEntry* ImportFile(const std::string& absSrc, const std::string& relDest);

    void SaveManifest() const;
    uint32_t HealthIssues() const { return healthIssues_; }

private:
    static AssetType TypeOf(const std::string& relPath);
    static uint64_t HashFile(const std::string& absPath);
    /// 读 sidecar .meta（无/坏 → 0）；wantWrite = 缺失时按 e 现值写一份
    void SyncMeta(AssetEntry& e) const;

    std::string root_;
    std::vector<AssetEntry> entries_; // relPath 升序（含墓碑）
    // 启动期 manifest 携带（path → guid/spriteId；首轮 Rescan 后清空）
    std::unordered_map<std::string, std::pair<uint64_t, uint32_t>> manifestCarry_;
    uint32_t nextSpriteId_ = 0;
    uint32_t spriteIdBase_ = 0;
    uint32_t healthIssues_ = 0;
    bool opened_ = false;
    ChangeSet lastChange_;
};

} // namespace lemon::editor
