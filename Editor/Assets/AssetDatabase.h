// Lemon 编辑器 — 资产数据库（M4.md §5 M4.4；06 §2 GUID/.meta/manifest）
// 纯文件系统 + JSON 逻辑，零 GPU/ImGui 依赖（可单测；GPU 侧见 AssetGpuCache）。
//   * GUID 稳定键：.meta 随文件走（重命名/移动引用不断）；.scene 只存 GUID/spriteId。
//   * spriteId 持久分配（manifest 记账，只增不减）：已存场景引用不因增删资产漂移。
//   * 删除文件 = 条目出表（墓碑机制 2026-10-01 退役，06 §2.2 修订——误删恢复由
//     .meta 随文件走 + 版本管理承担；编号只增不减继续成立）。孤儿 .meta：零引用
//     自动清扫、仍被引用保留 + 红字（SweepOrphanMetas 判据，Unity/Cocos 同款收口）。
//   * 启动/重扫体检（GUID 冲突 / 低 32 位碰撞 / 已删仍被引用）红字进 Console。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lemon::editor {

/// 原子落盘（共享工具，2026-09-24 审查 F-04/P-13）：同目录 .tmp 全量写入 + flush
/// 显式校验 + rename 替换——磁盘满/进程中断只丢 .tmp，不把原文件截成半档。
/// 场景/Prefab/存档/manifest 四条保存链统一走此口。
/// durable（M7a 批① M21）= rename 前对 .tmp fsync：掉电后名字交换至多回到旧档，
/// 不会出现长度 0 的新档；高频写（.meta/场景）不必开，manifest/存档等"重建代价
/// 高"的落盘点开。
bool WriteFileAtomic(const std::string& path, const void* data, size_t n,
                     bool durable = false);
inline bool WriteFileAtomic(const std::string& path, const std::string& s,
                            bool durable = false) {
    return WriteFileAtomic(path, s.data(), s.size(), durable);
}

// Table = .tab 配置表资产（M6a 批②，ADR-012；.meta/manifest 按 AssetTypeName 字符串
// 序列化 → 枚举插位自由，历史档不受影响）
// AnimSet = .override 动画集容器（M6a 批② T3c：段名 → .anim 引用清单；Unity
// AnimatorController 壳——段身份仍是 .anim 文件 GUID，集只存引用）
// Controller = .controller 动画状态机（M6a 批② T3d，ADR-013：状态词表 + 过渡
// 条件 + 参数表；World 级 ControllerTable，AnimGraphSystem #16 消费）
// Rml/Rcss = 游戏 UI 文档/样式表（M6b 批③b，ADR-014：一屏 = 一文档；.rcss 经
// <link> 引用；双击 .rml 装载到游戏 UI，热重载走 Rescan ChangeSet 消费）
// Audio = 音频源（M6c 竖切批，ADR-015：.wav/.ogg/.mp3/.flac → 烤制 .lemon/baked/
// audio/ 的 LBA1；浏览器过滤/图标/试听随批① 正式落）
enum class AssetType
    : uint8_t { Sprite, Prefab, Script, Clip, Table, AnimSet, Controller, Rml, Rcss, Audio,
                Generic };

const char* AssetTypeName(AssetType t);
struct AssetEntry {
    uint64_t guid = 0;
    std::string relPath;   // 相对项目根（'/' 分隔，含扩展名；06 §1：Assets/** 与根级 Prefabs/**）
    AssetType type = AssetType::Generic;
    uint32_t spriteId = 0; // Sprite：AtlasRegistry 稳定 id（0 = 非 sprite）
    uint64_t hash = 0;     // 内容 FNV-1a 64（重导入判定）
    bool missing = false;  // 墓碑 2026-10-01 退役：仅剩编辑器内 Remove() 的同帧隐藏位
                           //（下轮 Rescan 出表；不就地 erase = 调用方持有 entries_ 引用）。
                           // 恒 false 的消费方守卫清除随 M7a 批② AssetIndex 搬运批。

    // ---- 网格切片（M5 批③；.meta importer 段声明，DB 纯文件系统零解码记账）----
    // cellW/H=0 = 全幅（M4 最小集语义）；gridCols/Rows 来自 meta frames 声明
    // （yami .anim hframes/vframes 同款）→ Rescan 分配连号块 base..base+count-1
    // （manifest 持久，跨会话稳定——场景引用/clip 解析可复现；号只增不减）。
    uint16_t cellW = 0, cellH = 0;
    uint16_t gridCols = 0, gridRows = 0;
    uint32_t sliceBase = 0;  // 0 = 无块
    uint32_t sliceCount = 0; // = gridCols*gridRows（块分配时刻的值）
    bool Sliced() const { return sliceBase != 0 && sliceCount != 0; }

    // ---- 音频 importer（M6c 批①，ADR-015：.meta importer 段；每次重扫重读）----
    float audioLoopStart = 0.0f; // 秒；烤制期换算帧写 LBA1 头（0/0 = 全曲循环）
    float audioLoopEnd = 0.0f;
    bool audioPreload = false;   // true = 整载 RAM（批①b 流式落地前的显式覆盖位）
    /// 切片序号（行优先）→ spriteId（越界 = 0）
    uint32_t SliceSpriteId(uint32_t cell) const {
        return cell < sliceCount ? sliceBase + cell : 0;
    }

    std::string FileName() const; // relPath 末段
    std::string Dir() const;      // relPath 去末段（"" = 根；无尾 '/'）
};

class AssetDatabase {
public:
    struct ChangeSet {
        std::vector<uint64_t> added, modified, removed;
        bool Empty() const { return added.empty() && modified.empty() && removed.empty(); }
    };

    /// 孤儿 .meta 清扫报告（2026-10-01 拍板：Unity/Cocos 式自动清 + 引用判据保守保留）
    struct OrphanSweepResult {
        std::vector<std::string> cleaned;        // 已清（源缺失且 guid 零引用）
        std::vector<std::string> keptReferenced; // 保留（guid 仍被引用 = 复链钩子）
    };
    /// 手动清扫入口（Assets 菜单）：与 Rescan 自动路径同判定，立即执行并返回报告。
    OrphanSweepResult SweepOrphanMetas();

    /// 打开项目（root 含 Assets/；缺则建空目录）。spriteIdBase = 程序化图集之后
    /// 首个可用 id（调用方 = ViewportRenderer 装配后 SpriteCount()+1）。
    /// 扫描 + meta 补齐 + manifest 载入/落盘 + 体检。返回 false = root 不可写。
    bool OpenProject(const std::string& projectRoot, uint32_t spriteIdBase);

    /// 重扫（FileWatcher 触发/手动）：保 guid/spriteId；产出 LastChange。
    /// removed 事件「保留至被消费」（review 2026-10-02 #7）：Remove() 预入队的
    /// 删除事件经 Rescan 传递给消费者，重扫不清空（跨 Rescan 不丢）；消费者处理
    /// 完调 ConsumeChange() 取走清零，不消费则下次原样再交付（不重复处理由
    /// 消费者清零保证）。
    void Rescan();

    const ChangeSet& LastChange() const { return lastChange_; }
    /// 消费者取走变更集（处理完 LastChange 后调用——防跨重扫重复处理）
    void ConsumeChange() { lastChange_ = {}; }

    // ---- 查询（外部删除 = 条目同轮出表；missing 仅编辑器内 Remove 的同帧过渡位）----
    const AssetEntry* FindByGuid(uint64_t guid) const;
    const AssetEntry* FindByPath(const std::string& relPath) const;
    const AssetEntry* FindBySpriteId(uint32_t spriteId) const;
    /// spriteId 是否已登记（全幅号 ∪ 切片连号区间；M5 批④ 场景装载悬空校验用）
    bool SpriteIdRegistered(uint32_t spriteId) const;
    /// clip 资产按 GUID 低 32 位反查（Animator2D.clipId 槽显示/解析；M5 批③）
    const AssetEntry* FindClipByLowId(uint32_t lowId) const;
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
    std::string AssetsRoot() const;                     // root/Assets（导入落点）
    /// 程序化图集基号（OpenProject 入参留存）：id < 基号 = 程序化页，无需 DB 记账
    /// （场景悬空 spriteId 体检用——EditorContext::OpenScene，2026-09-22 测试报告观察 6）
    uint32_t SpriteIdBase() const { return spriteIdBase_; }
    std::string AbsolutePath(const AssetEntry& e) const { return root_ + "/" + e.relPath; }
    /// 16 位 hex（Inspector 槽显示 / C# Assets.SpriteOf 参数形态）
    static std::string GuidToHex(uint64_t guid);
    static uint64_t HexToGuid(const char* hex);

    // ---- 编辑器操作（同步落盘；guid 稳定 → 场景引用不断）----
    /// 重命名/移动（相对 Assets/ 的新路径）。失败（目标存在/IO 错）false。
    bool Rename(AssetEntry& e, const std::string& newRelPath);
    /// 配置/更新网格切片（M6a 批② T3b-3：写 .meta importer 段，读改写原子；
    /// guid/type/hash 保原值）。全零 = 撤销切片转整图。生效 = 调用方随后
    /// Rescan()（连号块分配 / frames 增大烧号）。false = 非 sprite / 写盘失败。
    bool SetGridSlice(AssetEntry& e, uint32_t cellW, uint32_t cellH, uint32_t cols,
                      uint32_t rows);
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
    /// 新发号：全宽全库唯一 + 同类型域内低 32 位唯一（运行时映射约定的安全前提，
    /// 2026-10-01 svr-test Player/Mob 低 32 位碰撞实证后立；Rescan 体检兜手工 .meta）
    uint64_t GenerateUniqueGuid(AssetType type) const;
    /// 引用面检索：项目数据文本（scene/prefab/anim/override/controller/tab/rml/rcss/
    /// cs/asset，不含 .meta——自引用假阳性）中 guid 的任一形态（hex 小写/大写/十进制）
    /// 命中即真。语料惰性装配，Rescan/OpenProject 起点失效。
    bool GuidReferenced(uint64_t guid);
    /// 清扫本体（自动/手动共用）：遍历源缺失的 .meta，零引用删之、被引用留之。
    OrphanSweepResult SweepOrphanMetasInternal();
    // 引用语料增量缓存（review 2026-10-02 #24）：路径 → {mtime,size,text}，失效后
    // 只重读变更/新增文件、复用未变内容——此前单一大串每次 Rescan 全量重建
    //（保存/删除触发重扫即全项目 IO，大项目同步卡顿 + 瞬态大分配）
    struct RefFile {
        uint64_t mtime = 0;
        uintmax_t size = 0;
        std::string text;
    };
    std::unordered_map<std::string, RefFile> refFiles_;
    bool refCorpusTried_ = false; // true = refFiles_ 为本 Rescan 周期语料

    // 内容哈希增量缓存（review 2026-10-02 #31，refFiles_ 同款）：路径 →
    // {mtime,size,hash}。原 Rescan 对全项目资产无条件全文件重读算哈希——watcher
    // 500ms 轮询下任一文件改动即全量同步 IO 卡 UI 线程；mtime+size 未变 = 字节
    // 未变（编辑器/保存器都更新 mtime），直接复用上轮哈希值。
    struct HashStat {
        uint64_t mtime = 0;
        uintmax_t size = 0;
        uint64_t hash = 0;
    };
    std::unordered_map<std::string, HashStat> hashCache_;

    std::string root_;
    std::vector<AssetEntry> entries_; // relPath 升序（含墓碑）
    // 启动期 manifest 携带（path → guid/spriteId/切片块；首轮 Rescan 后清空）
    struct CarryInfo {
        uint64_t guid = 0;
        uint32_t spriteId = 0;
        uint32_t sliceBase = 0;
        uint32_t sliceCount = 0;
    };
    std::unordered_map<std::string, CarryInfo> manifestCarry_;
    uint32_t nextSpriteId_ = 0;
    uint32_t spriteIdBase_ = 0;
    uint32_t healthIssues_ = 0;
    bool opened_ = false;
    ChangeSet lastChange_;
};

} // namespace lemon::editor
