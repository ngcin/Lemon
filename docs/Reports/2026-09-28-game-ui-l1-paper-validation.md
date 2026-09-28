# L1 机制契约纸面验证包（ADR-014 附录 B）

- 日期：2026-09-28
- 性质：一次性设计快照——[ADR-014](../ADR/ADR-014-Game-UI-RmlUi-Integration.md) 冻结 M1–M8 前的闭合性验证证据；契约后续修订以 ADR 为准，本文件不回改。
- 方法：取四品类中最刁钻的四个屏幕，按"文档结构（.rml）→ C# 调用序列 → 事件流 → 消费机制 → 应力点"推演。**判据：不触发结构逃生舱（Create/Append/Remove）即走通 = 契约闭合。**

| 屏幕 | 品类来源 | 消费机制 | 结论 |
|---|---|---|---|
| ⓪ 升级三选一卡片 | VS（批③d 首个迁移） | M1/M2/M3 | 闭合 |
| ① 局外图鉴/收集 | VS（批③e 消费者） | M1/M2/M3 + 存档 meta 档 | 闭合（两条登记） |
| ② TD 建造栏 + 小地图 | 塔防（M6c 波2） | M1/M2/M3/M6/M7 | 闭合（摆塔拖放属 M4 波3，契约已留位） |
| ③ 剑阁式物品栏（拖放+对比） | 英雄守图/ARPG（波3） | M2/M3/M4/M5 | 闭合（三条契约细则在此屏定形） |

---

## ⓪ 升级三选一卡片（基线用例：M2 最小闭环）

**需求**：升级时冻结游戏，弹 3~5 张卡（数量运行时定）；每卡 = 图标 + 名称 + 描述 + 品级色边框 + 贴图背景；点击或数字键选择；死亡对话框同通道单按钮形态。

**文档结构** `upgrade_cards.rml`：

```html
<div id="panel" class="modal">
  <h1 id="title" data-field="title"/>
  <div id="cards" data-template="card">
    <template data-name="card">
      <button class="card {{rarity}}" data-event="pick">
        <img data-field="icon"/>
        <div data-field="label"/>
        <div data-field="sub"/>
      </button>
    </template>
  </div>
  <div class="hint">点击或按数字键选择</div>
</div>
```

**C# 调用序列**（PlayerCombat.UpdateCards 改写）：

```csharp
Time.Scale = 0f;
UI.Show("upgrade_cards", modal: true);            // M1：模态 = 输入让出（M7）
UI.SetText("title", "升级！三选一");
UI.SetItems("cards", rolls.Select(c => new UiItem {
    Key    = c.id,                                // 稳定 key：事件回传认它，不认下标
    Fields = { ["icon"]=c.IconGuid, ["label"]=c.Label,
               ["sub"]=c.Desc,    ["rarity"]=c.Grade } }));
UI.Apply();                                       // 每帧一次批量提交（M2）
```

**事件流**（下一帧事件队列）：

```
UiEvent{ doc="upgrade_cards", key="whip_lv2", type="pick" }
→ C# 应用升级 → UI.Hide("upgrade_cards") → Time.Scale = 1f
```

**契约细节验证点**：

- 品级色 = `rarity` 字段落到条目根节点为 class 追加，`.card.epic { border-color:#a335ee; }` 全在 RCSS——**改品级配色 = 改样式资产，引擎零改动**；
- 数字键选择 = 键盘焦点导航（M7 手柄/键盘同一条机制），非专门通道；
- **响亮失败示例**：C# 误写 `["lable"]` → 引擎控制台 `field 'lable' not in template 'card' (has: icon,label,sub,rarity)` + stats 契约错误计数 +1，smoke 断言零契约错误兜底——string id 的"静默失效"死法被制度性杀死；
- 单按钮死亡对话框 = 同文档 `SetItems` 只给 1 条（空槽不渲染语义随迁）。

**结论**：M1/M2/M3 最小闭环成立，现存 RtUiCards 的全部语义（模态冻结/消费式回读/数字键）均有对应物。

---

## ① 局外图鉴/收集屏（列表 + 状态 + 双栏）

**需求**：分类 tab（武器/被动/…），条目网格（数量随内容更新增长），锁定/解锁态，点选看详情（右侧栏），进度持久化到 meta 档。

**文档结构** `codex.rml`：

```html
<div id="codex" class="screen">
  <div id="tabs" data-template="tab">
    <template data-name="tab">
      <button class="tab {{active}}" data-event="tab"><span data-field="name"/></button>
    </template>
  </div>
  <div id="grid" data-template="entry">
    <template data-name="entry">
      <button class="cell {{locked}} {{rarity}}" data-event="select">
        <img data-field="icon"/>
        <div data-field="name"/>
      </button>
    </template>
  </div>
  <div id="detail" class="pane">
    <h2 id="detail/name"/>
    <div id="detail/desc" class="rich"/>
    <div id="detail/stats"/>
    <div id="detail/count"/>
  </div>
</div>
```

**C# 调用序列**：

```csharp
void OpenCodex(string tab = "weapons") {
    UI.Show("codex");
    Refill(tab);                                   // 开屏与 DocumentReloaded 共用
}
void Refill(string tab) {                          // 热重载重灌入口（M2 契约）
    UI.SetItems("tabs", cats.Select(c => new UiItem {
        Key = c.Id, Fields = { ["name"]=c.Name, ["active"]=(c.Id==tab ? "active":"") }}));
    UI.SetItems("grid", entries[tab].Where(v => vSeen(v.Id)).Select(v => new UiItem {
        Key = v.Id, Fields = { ["icon"]=v.IconGuid, ["name"]=v.Name,
                               ["locked"]=v.Unlocked? "":"locked",
                               ["rarity"]=v.Grade }}));
    FillDetail(current);                           // SetText×3 + SetInnerRml(stats)
    UI.Apply();
}
```

**事件流**：`UiEvent{key="passives", type="tab"}` → Refill("passives")；`UiEvent{key="holy_lv3", type="select"}` → FillDetail → 解锁计数写 meta 档（批② T5 通道）。

**消费机制**：M1（屏幕）+ M2（SetItems×2 + SetClass 语义经字段 + SetText/SetInnerRml）+ M3（tab/select）；持久化 = 存档 meta 档（已落地），**UI 层零新增**。

**应力点与登记**：

1. **数量**：全品类图鉴量级 100~500 条，RmlUi 直接克隆扛得住（ADR-008 10k 元素 3ms 预算内），**不做虚拟化**；>500 且实测超预算再启动（稳定 key 设计已预留）。
2. **分类集合动态性**：tab 数量设计期可知 → 文档静态容器 + SetItems 即可。若未来出现 mod 式运行时增类 → 触发结构逃生舱评估（ADR-014 D2 三信号之一），**登记不实现**。
3. 双栏布局/网格换行/滚动 = 纯 RCSS（flex wrap / overflow scroll），无机制。

**结论**：闭合。图鉴是批③e 的验收屏幕。

---

## ② TD 建造栏 + 波次横幅 + 小地图（波2 用例）

**需求**：底部建造栏（塔按钮 + 造价 + 金币不足置灰），点击进入摆塔模式（世界幽灵预览、世界点击落位），波次来袭横幅，右上小地图（世界 RT）。

**文档结构** `td_hud.rml`：

```html
<div id="hud">
  <div id="topbar">
    <span id="gold"/><span id="lives"/><span id="wave"/>
  </div>
  <div id="minimap-host"><img id="minimap"/></div>   <!-- M6：RT 纹理源 -->
  <div id="buildbar" data-template="bt">
    <template data-name="bt">
      <button class="bt {{afford}}" data-event="build">
        <img data-field="icon"/>
        <div data-field="cost"/>
      </button>
    </template>
  </div>
</div>
<div id="banner" class="toast hidden"/>              <!-- M1：toast 层固定位 -->
```

**C# 调用序列**：

```csharp
// 开局一次
UI.Show("td_hud");
UI.SetItems("buildbar", towers.Select(t => new UiItem {
    Key = t.Id, Fields = { ["icon"]=t.IconGuid, ["cost"]=t.Cost.ToString() }}));
UI.BindTexture("minimap", RTSource.MinimapCamera);   // M6：RT 源绑定
// 每帧（金币变化时）
foreach (var t in towers)
    UI.SetClass($"bt/{t.Id}/root", "afford", gold >= t.Cost);
UI.SetText("gold", gold.ToString());
UI.Apply();
// 波次来袭
UI.SetText("banner", $"第 {n} 波 · {name}"); UI.ShowToast("banner", 2.5f);
```

**事件流**：`UiEvent{key="arrow_tower", type="build"}` → C# 进入摆塔模式（`InputMode.Placement`，M7：世界输入接管、UI 仅 hover 高亮）→ 世界点击（既有输入/射线通道，**不经过 UI**）→ ECS 落塔 → 退出摆塔模式。

**消费机制**：M1（toast 层）+ M2（SetItems/SetClass 置灰/SetText）+ M3（build 事件）+ M6（图标资产源 + 小地图 RT 源 + 冷却扫描帧序源）+ M7（摆塔模式输入路由）。

**应力点**：

1. **UI↔世界输入交错**是本屏核心：摆塔模式下鼠标点世界 = 游戏输入；悬停建造栏 = UI 输入——M7 的模态/让出规则覆盖，**无需新机制**，但 M6c 时要补一条 smoke（摆塔中点 UI 不落塔）。
2. **拖到世界摆塔**（按住拖出）= M4 世界坐标落点用例，波3 才实现；v1 点选式摆塔为品类通行做法（Bloons 系同款），**契约已留位不提前实现**。
3. 造价/金币每帧 SetClass 量级 = 建造栏按钮数（<20），批量 Apply 单次提交，开销可忽略。

**结论**：闭合（拖放变体后置有契约位）。

---

## ③ 剑阁式物品栏（波3：拖放 + 对比 tooltip 全量用例）

**需求**：6 格英雄物品栏，拖动物品换格/整理；拖到商店区出售；悬停看详情（富文本：彩色属性段），与已装备物品对比（双面板）。

**文档结构** `inventory.rml` + `tooltip.rml`（独立文档，M5 层固定位）：

```html
<div id="inv">
  <div id="slots" data-template="slot">
    <template data-name="slot">
      <div class="slot" data-droppable="slot">
        <img data-field="icon"/><span data-field="count"/>
      </div>
    </template>
  </div>
</div>
<div id="shop" class="zone" data-droppable="sell"/>   <!-- 拖入即出售区 -->
```

**C# 调用序列与事件流**（M4 全生命周期）：

```
DragStart{ key="slot/3" }            → C# 无需响应（拖拽视觉引擎托管：源元素快照跟随光标）
DragEnter{ target="slot/5" }         → 引擎已自动施加 .drop-hover 高亮（RCSS）；C# 可选预校验
DragLeave{ target="slot/5" }         → 高亮引擎自动撤销
Drop{ source="slot/3", target="slot/5" }   → C# 结算交换/合并 → SetItems("slots", …) 刷新
Drop{ source="slot/3", target="sell" }     → C# 出售 → 金币/格子刷新
DragCancel                           → C# 无需响应（视觉引擎已回收）
hover{ key="slot/3" }                → C# 填 tooltip 文档（SetInnerRml 富文本）+ 钉住对比面板 = 普通文档
```

**在本屏定形的三条 M4/M5 契约细则**（ADR-014 D2 已收录）：

1. **边沿触发不逐帧**：DragOver 若逐帧发事件 = 事件洪水；契约只发 Enter/Leave/Drop。
2. **UI 载身份、游戏持语义**：拖拽视觉与高亮 = 引擎表现职责；"能否放、放下发生什么"全在 C#（Drop 结算前不写任何状态）。
3. **droppable 在文档声明**（`data-droppable`），高亮类引擎自动施加——C# 不逐目标管理高亮。

**消费机制**：M2（SetItems/SetInnerRml）+ M3（hover）+ M4（全事件生命周期）+ M5（tooltip 托管）。装备槽 = 同结构第二个容器；拖到世界（丢地上）= M4 世界坐标落点位，同批可用。

**结论**：闭合，但依赖波3 机制——**契约冻结于波1（事件结构/payload 世界坐标位），实现后置于波3**。

---

## 总结论

1. 四屏全部走通，未触发结构逃生舱——**M1–M8 契约闭合性成立**，ADR-014 D2 可冻结。
2. 契约 indebted 项（非机制缺口）：图鉴分类动态性（登记）、虚拟化（>500 条且超预算再启）、摆塔拖放变体（M4 波3）、DragEnter/Leave 边沿触发与 droppable 声明式（已入契约）。
3. 本验证包同时充当批③c/③d/③e 的**验收样例底稿**——实现期各屏幕的 smoke 断言即按本文档的调用序列与事件流写。
