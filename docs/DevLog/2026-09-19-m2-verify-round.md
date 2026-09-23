# 2026-09-19 · M2 验证轮（复核清单问题：8 项全属实，ISSUE-2 ASan 实锤，Engine 仍零修改）

对 M2 复核清单（`../Reports/2026-09-19-m2-review-checklist.md`）登记问题逐项独立复核（源码逐行 + Release 复跑 12821 OK +
ASan 探针）。结论：全部属实，其中 1 项范围修正、1 项新补登：

- **ISSUE-2 越界实锤**：`{"count":200}` 无数组键读入 → StatSystem tick 即
  `heap-buffer-overflow @ Systems.cpp:501`（1000 实体命中池边界；8 实体越界落
  池内、ASan 静默）。**范围修正**：StateHash 有 clamp（StateHash.cpp:63）
  安全，越界消费方仅 StatSystem——清单初版误报已更正。
- **补登 ISSUE-8（P3）**：Equipment.relicIds 字段+数组段同名双登记 → 合法档每次
  读入必发一条 type mismatch 假告警（数据无损）。
- ISSUE-5 后果确认更重：Flee 实体速度被主动清零并覆盖 Chase 速度 → 完全冻结。
- 清单引用更正两处（E10→N5、P6→N10），详见 Checklist §7.1。
