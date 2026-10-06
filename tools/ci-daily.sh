#!/bin/bash
# Lemon 本机每日 CI 门禁一键脚本（M7a 批⑧ 2026-10-06；**2026-10-07 用户拍板 =
# 手动档**——launchd 定时未装载（模板留档 Tools/com.lemon.ci-daily.plist），要跑
# 门禁手动执行本脚本）
# 用法：tools/ci-daily.sh [--skip-build]
# 编排：增量构建 → editor-regression.sh full（20 步，含 bench-survivor 门禁步）
# 报告：build/ci-reports/YYYY-MM-DD.log（HEAD/日期/逐步/汇总；连续看 = flake 观察面）
# 纪律：不 git pull、不动工作树——WIP 半成品编译失败 = 报告标 BUILD-FAIL（环境
# 态）非回归红；回归守卫 ABORT（交互会话在场）同理透传 rc=2 供报告区分。
set -u
cd "$(dirname "${0}")/.."

# launchd 环境 PATH 最小（/usr/bin:/bin…），brew 工具链补位（本机 x64 = /usr/local，
# arm 兼容 /opt/homebrew）
export PATH="/usr/local/bin:/opt/homebrew/bin:${PATH}"
export LEMON_NO_ACTIVATE=1

REPORT_DIR="build/ci-reports"
mkdir -p "${REPORT_DIR}"
LOG="${REPORT_DIR}/$(date +%Y-%m-%d).log"

{
echo "== Lemon ci-daily $(date '+%Y-%m-%d %H:%M:%S') =="
echo "HEAD: $(git rev-parse --short HEAD 2>/dev/null || echo no-git) — $(git log -1 --format=%s 2>/dev/null | head -c 100)"
echo

echo "-- incremental build (preset mac) --"
if [ "${1:-}" = "--skip-build" ]; then
    echo "(skipped by flag)"
elif cmake --build --preset mac >"${LOG}.build" 2>&1; then
    echo "build OK ($(tail -1 "${LOG}.build" | head -c 120))"
else
    echo "BUILD-FAIL（工作树 WIP 或依赖变动——回归未跑；非回归红）"
    tail -5 "${LOG}.build"
    exit 1
fi
rm -f "${LOG}.build"

echo
Tools/editor-regression.sh full
rc=$?

echo
echo "== ci-daily done rc=${rc} (0=all green / 1=regression red / 2=guard abort=editor in use) =="
exit ${rc}
} >> "${LOG}" 2>&1
