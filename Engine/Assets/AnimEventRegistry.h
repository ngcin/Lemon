// Lemon 引擎 — 项目级帧事件注册表（M8 批⑬；Assets/Tables/anim-events.tab）
// 帧事件 id 的命名面单一权威：.clip events[] 的数字 id（u16 线上格式，永不迁移）
// 在装载期经本表解析为名字——作者见下拉、C# 见启动红字、运行时仍逐位比 u16
//（名字不入模拟态，金回放零重录，ADR-013 段名按名解析同款论证）。
//   * 表 = ADR-012 .tab 轨道（JSON 全字符串格，解析复用 ParseTableJson），
//     列头即列契约：name / id / payload / desc（payload/desc 批⑬c/⑭ 消费，本批
//     原样保留）。
//   * id 语义（GUID 同款稳定）：分配后永不变更、删除不回收复用；「新建事件」
//     分配 = 当前最大 id + 1；手写表允许跳号。
//   * 进程级单份、宿主注入（编辑器 = 开项目/Rescan 且 Play 中冻结快照；lemon-game
//     = 启动装配脚本宿主前；script-tests = 夹具）。缺表/坏表 = 裸 id 模式照常
//    （ADR-012 缺表保底哲学——编辑器降级显 #N（未命名），C# Define/OnFrame 红字）。
//   * 线程口径：注入只发生在 Edit/装配期（主线程），Play 期只读（域线程订阅期
//     一次解析后 C# 自缓存）——快照语义同 BuildPlayTableCache，无并发写。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lemon::assets {

/// 注册表条目（AnimEventAt 全量读取——编辑器下拉/测试用；行序 = 表行序）
struct AnimEventDef {
    std::string name;
    uint32_t id = 0;     // 1..65535（0 保留 = ClipEventEdit 缺省）
    std::string payload; // 批⑬c 类型化声明（"—" / "float:force"…）
    std::string desc;
};

/// 注入/替换注册表（.tab JSON 全文本）。
///   * 空文本 = 清空（裸 id 模式，合法——缺表保底）；
///   * 坏 JSON / 列头不符列契约 = 整表拒绝（空表 + error，false）；
///   * 行级问题（名字非法 / id 非法 / 同名 / 同 id）= 该行跳过 + WARN（表内其余
///     照常——warn-once 哲学，残留面归批⑬b lint）；
///   * 与上份快照同名异 id（手改既有行 id）= WARN「应新建行替代」（每
///     (name, oldId, newId) 一次）。
bool SetAnimEventTable(std::string_view tabJson, std::string* error = nullptr);

/// 名字 → id；0 = 未注册/无表（桥语义源点：-1 的 C# 侧红字）。装载期低频调用，
/// 热路径零参与（C# 订阅期一次解析后自缓存——id 永不变更使其安全）。
uint32_t AnimEventIdByName(const char* name);

/// id → 名；nullptr = 未注册（编辑器 chips/badge 显示面）。
const char* AnimEventNameById(uint32_t id);

/// 已注册事件数（下拉/测试）。
size_t AnimEventCount();

/// 按表行序取条目；越界 = false。
bool AnimEventAt(size_t i, AnimEventDef& out);

/// 下一个可分配 id = 当前最大 id + 1（空表 = 1）——编辑器「新建事件」分配单源。
uint32_t AnimEventNextId();

/// 注册表固定路径契约（宿主寻址用；查找大小写不敏感——项目既有 tables/ 目录
/// 大小写不一，macOS/Windows 文件系统亦不敏感）。
inline constexpr const char* kAnimEventTableRelPath = "Assets/Tables/anim-events.tab";

/// 事件名合法性（单源：引擎装载行检 + 编辑器「新建事件」预检共用）：
/// 非空 / ≤64 字节 / 无控制字符与引号（跨界窄串 + 下拉字面约束）。
bool ValidAnimEventName(std::string_view n);

// ---- 订阅者声明面 + 反向 lint（M8 批⑬b）------------------------------------
// 订阅者集合 = AnimEventIdByName 桥的查询名记录：C# 侧每个名字每域首解析必过桥
//（AnimEvents.TryIdOf 缓存域装载起清空——Define/OnFrame 声明即查询），引擎侧
// 顺手记录即得，零新增 vtable 槽、零 C# 改动；编辑器 C++ 走 NameById 不经此面
//（无编辑器噪声）。SetAnimEventTable 注入即清（表换 = 比较基线换；编辑器同表
// 重注入被 guid/hash 变更门拦 → 跨 Play 退出保留，Edit 态 lint 见"上次 Play"面）。

/// 已记录订阅者数（行序 = 首查询序，确定）。
size_t AnimEventSubscriberCount();
/// 按首查询序取订阅者名；越界 = false。
bool AnimEventSubscriberAt(size_t i, std::string& out);

/// 项目内 clip 打点用量（lint 调用方聚合——编辑器扫全部 .clip events[].id，
/// 含当前编辑未保存态）。
struct AnimEventUsage {
    std::string clip; // 显示名（clip 文件名）
    uint32_t id = 0;
};

/// lint 三件结果项（kind 定序 ①②③；调用方按 kind 取 i18n 模板格式化）。
struct AnimEventLintItem {
    int kind = 0;     // 1=打点未注册 2=注册名无订阅者 3=订阅名无打点（info）
    std::string name; // ②③ = 事件名；① = 空
    uint32_t id = 0;  // 关联 id
    std::string clip; // ① = 打点所在 clip；②③ = 空
};

/// 反向 lint 三件（批⑬b；纯函数——used = 全项目 clip 打点聚合，同 (clip,id)
/// 去重）：① used 中 id 未注册（作者笔误面，badge 逐处的聚合告警）② 注册名
/// ∉ 订阅者（漏接/死事件；订阅者面为空 = 本会话未装载过脚本 → 整组抑制不误报）
/// ③ 订阅名已注册但全项目无打点（死订阅，info）。未注册订阅名不进 ③（⑬a
/// 启动红字已覆盖，不双报）。结果按 kind 稳定定序（①used 序 / ②表行序 /
/// ③首查询序）。
std::vector<AnimEventLintItem> AnimEventLint(const std::vector<AnimEventUsage>& used);

} // namespace lemon::assets
