#!/bin/bash
# Lemon 编辑器自动化回归（Editor-Manual-Test-Guide.md §1）
# 用法：tools/editor-regression.sh [quick|full] [build-dir]
#   quick     = ctest + 基础冒烟 + 关闭状态机（约 1 分钟）
#   full      = 缺省；追加 资产链/脚本链/终验/场景 roundtrip
#   build-dir 缺省 build/mac（cmake --preset mac 产物）
# 注：macOS 自带 bash 3.2——所有展开一律 ${BRACED}（变量名后跟全角标点会把
#     高位字节并进名字 → "unbound variable"）。
set -u
cd "$(dirname "${0}")/.."
MODE=full
BUILD=build/mac
for a in "$@"; do
    case "${a}" in quick|full) MODE="${a}" ;; *) BUILD="${a}" ;; esac
done
EDITOR="${BUILD}/Editor/lemon-editor"
SDK="${BUILD}/Scripting/dotnet/TestScript.dll"
TMP="$(mktemp -d /tmp/lemon-regress.XXXXXX)"
trap 'rm -rf "${TMP}"' EXIT

pass=0
fail=0
step() { # step <名称> <命令...> — 退出码 0 = PASS
    local name="${1}"; shift
    if "$@" >/dev/null 2>&1; then
        printf '  OK   %s\n' "${name}"; pass=$((pass+1))
    else
        printf '  FAIL %s\n' "${name}"; fail=$((fail+1))
    fi
}
grep_step() { # grep_step <名称> <期望子串> <命令...>
    local name="${1}" want="${2}"; shift 2
    local out; out="$("$@" 2>&1)"
    if echo "${out}" | grep -q "${want}"; then
        printf '  OK   %s\n' "${name}"; pass=$((pass+1))
    else
        printf '  FAIL %s (want: %s)\n' "${name}" "${want}"
        echo "${out}" | tail -5 | sed 's/^/       /'; fail=$((fail+1))
    fi
}

echo "== Lemon editor regression (mode=${MODE}, build=${BUILD}) =="

echo "-- ctest --"
step "ctest 3/3 (engine-tests/imgui-isolation/script-tests)" \
    ctest --test-dir "${BUILD}" --output-on-failure

echo "-- editor smoke --"
grep_step "basic smoke (shell/panels/font/ID sweep)" "editor-smoke PASS" \
    "${EDITOR}" --smoke --frames 120
grep_step "smoke-close clean (clean scene exits at once)" "OK" \
    "${EDITOR}" --smoke-close clean --frames 300
grep_step "smoke-close dirty (dirty scene confirm)" "OK" \
    "${EDITOR}" --smoke-close dirty --frames 300

if [ "${MODE}" = "full" ]; then
    grep_step "asset-chain smoke (import/hot-replace/thumbnail)" "editor-smoke PASS" \
        "${EDITOR}" --project "${TMP}/assets" --smoke --frames 240
    if [ -f "${SDK}" ]; then
        grep_step "script-chain smoke (CoreCLR/spawn/play byte-exact/--validate)" "editor-smoke PASS" \
            "${EDITOR}" --project "${TMP}/script" --script "${SDK}" --smoke --play --frames 240 --validate
    else
        echo "  SKIP script-chain (missing ${SDK})"
    fi
    grep_step "final acceptance (wizard/judgement/fps/hot-reload/statebag/autosave/cold)" "editor-smoke PASS" \
        "${EDITOR}" --final --smoke --project "${TMP}/final" --frames 620
    step "scene CLI roundtrip: --save-scene" \
        "${EDITOR}" --smoke --frames 60 --save-scene "${TMP}/rt.scene"
    grep_step "scene CLI roundtrip: --scene reopen" "editor-smoke PASS" \
        "${EDITOR}" --smoke --frames 60 --scene "${TMP}/rt.scene"
fi

echo "== summary: PASS=${pass} FAIL=${fail} =="
[ "${fail}" -eq 0 ]
