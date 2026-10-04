#!/bin/bash
# Lemon 编辑器自动化回归（Editor-Manual-Test-Guide.md §1）
# 用法：tools/editor-regression.sh [quick|full] [build-dir]
#   quick     = ctest + 基础冒烟 + 关闭状态机（约 1 分钟）
#   full      = 缺省；追加 资产链/脚本链/终验/场景 roundtrip
#   build-dir 缺省 build/mac（cmake --preset mac 产物）
# 后台跑：编辑器窗口不抢前台焦点（smoke 输入为合成事件注入，不依赖激活；
# 2026-09-24 用户反馈回归运行打断前台工作而加，机制见 Window.cpp LEMON_NO_ACTIVATE）
export LEMON_NO_ACTIVATE=1

# 残留实例前置守卫（2026-09-30 批③c-5 事故复盘：前夜泄漏的 headless 实例与
# 回归链争 GPU/窗口资源，帧锚定注入链间歇失败、三轮排障才现形）。分类处置：
# 带 smoke/bench/frames 等 CLI 旗标的 = headless 残留 → 清理后继续；无旗标的 =
# 疑似交互会话 → 中止交人裁决（自动杀交互会话 = 丢用户未存状态）。
stale_pids=""
live_pids=""
# M7a 批④：守卫面扩 lemon-game（独立运行时同争 GPU；交互局同样不自动杀）
for pid in $(pgrep -f 'lemon-editor|lemon-game' 2>/dev/null); do
    cmd="$(ps -p "${pid}" -o command= 2>/dev/null)" || continue
    [ -z "${cmd}" ] && continue
    # review 2026-10-02 #30：可执行名改取 ucomm——此前 "${cmd%% *}" 取首空格前段，
    # build 目录含空格时截成目录名 → 残留实例既不清理也不中止（守卫静默失效，
    # 正是本守卫要防的 GPU 争用场景）。ucomm = 可执行名，与路径空格无关。
    exe="$(basename "$(ps -p "${pid}" -o ucomm= 2>/dev/null)")"
    [ "${exe}" = "lemon-editor" ] || [ "${exe}" = "lemon-game" ] || continue
    case "${cmd}" in
        *--smoke*|*--bench*|*--frames*|*--final*|*--play*|*--scene*|*--save-scene*|*--screenshot*|*--gen-vs-template*)
            stale_pids="${stale_pids} ${pid}" ;;
        *) live_pids="${live_pids} ${pid}" ;;
    esac
done
if [ -n "${live_pids}" ]; then
    echo "== ABORT: lemon-editor 正在运行（疑似交互会话，不自动杀）：${live_pids}"
    echo "       关闭它或手动 kill 后重跑；headless 残留会被本守卫自动清理"
    exit 2
fi
if [ -n "${stale_pids}" ]; then
    echo "-- 清理残留 headless 实例（前次回归泄漏，与本次争 GPU）：${stale_pids}"
    kill ${stale_pids} 2>/dev/null
    sleep 1
    alive=""
    for pid in ${stale_pids}; do
        kill -0 "${pid}" 2>/dev/null && alive="${alive} ${pid}"
    done
    [ -n "${alive}" ] && kill -9 ${alive} 2>/dev/null && sleep 1
fi

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
grep_step "smoke-drag (viewport gizmo move+rotate+resize+zoom+sling+focus injection)" \
    "smoke-drag: .* => OK" \
    "${EDITOR}" --smoke-drag --frames 90 --no-reopen
grep_step "smoke-ui (real-person session: shortcuts/undo/scrub/save/play/rename/reparent/nav/layout)" \
    "smoke-ui: .* => OK" \
    "${EDITOR}" --project "${TMP}/ui" --smoke-ui --frames 160 --no-reopen

if [ "${MODE}" = "full" ]; then
    grep_step "asset-chain smoke (import/hot-replace/thumbnail)" "editor-smoke PASS" \
        "${EDITOR}" --project "${TMP}/assets" --smoke --frames 240
    grep_step "anim-chain smoke (grid slice + clip + animator frame mapping; M5-b3)" \
        "smoke-anim: .* => OK" \
        "${EDITOR}" --project "${TMP}/anim" --smoke-anim --frames 120 --no-reopen
    grep_step "template-chain smoke (wizard copy + build + play menu/run/cards/death/revive/results/restart/pause/settings/tomenu; M5-b4+M6b-b3d2)" \
        "smoke-template: .* => OK" \
        "${EDITOR}" --smoke-template --frames 3400 --no-reopen
    # M7a 批④：独立运行时全链（lemon-game 直渲染 swapchain：entry dll 解析/
    # AssetIndex/四缓存/存档装载/UI 点击进局/prefab 运行时 spawn/渲染提取/
    # 存档回写）。夹具 = vs-survivor 模板拷贝 + 脚本侧 dotnet build（"构建归
    # 编辑器/packager，lemon-game 只消费"口径在回归里由脚本代行）
    GAME="${BUILD}/Engine/Entry/lemon-game"
    if [ -x "${GAME}" ]; then
        cp -R Templates/vs-survivor "${TMP}/game"
        if dotnet build "${TMP}/game/Game/Game.csproj" -c Release \
                -o "${TMP}/game/.lemon/bin" >/dev/null 2>&1; then
            grep_step "game-smoke (lemon-game standalone runtime: four caches + ui-click-to-run + prefab spawn + direct render + saves; M7a-b4)" \
                "game-smoke: .* => OK" \
                "${GAME}" --project "${TMP}/game" --frames 900 --smoke
        else
            echo "  FAIL game-smoke (夹具 Game/ 编译失败：dotnet build ${TMP}/game)"
            fail=$((fail+1))
        fi
    else
        echo "  SKIP game-smoke (missing ${GAME})"
    fi
    # M7a 批⑤：出包链（lemon-packager 目录拷贝式：dylib 闭包 @rpath 重锚 + rpath
    # 卫生 + MoltenVK ICD 自举 + self-contained runtime + 预烤音频 + manifest.pkg.json
    # + 自检闭环/清单对账；包体**零参** --smoke = data/ 缺省路径端到端——"解包即跑"
    # 的机器面）。夹具复用上方 game-smoke 的 ${TMP}/game;fps ≥ 60 = 08 M7a 判据。
    PKGR="${BUILD}/Tools/packager/lemon-packager"
    if [ -x "${PKGR}" ] && [ -d "${TMP}/game" ]; then
        if "${PKGR}" --project "${TMP}/game" --runtime "${BUILD}" \
                --out "${TMP}/pkg" --force >"${TMP}/pkg-build.log" 2>&1 &&
           grep -q "pkg-selfcheck: .* => OK" "${TMP}/pkg-build.log"; then
            pkg_smoke_run() {
                local out
                out="$("${TMP}/pkg/lemon-game" --smoke --frames 900 2>&1)" || return 1
                echo "${out}" | grep -q "game-smoke: .* => OK" || return 1
                echo "${out}" | grep -o "fps=[0-9.]*" | head -1 | awk -F= '{ exit !($2+0 >= 60) }'
            }
            step "pkg-smoke (packager clean-dir bundle: closure+sc-runtime+baked-audio, zero-arg run fps>=60; M7a-b5)" \
                pkg_smoke_run
        else
            echo "  FAIL pkg-smoke (出包/自检失败，日志 ${TMP}/pkg-build.log)"
            tail -5 "${TMP}/pkg-build.log" 2>/dev/null | sed 's/^/       /'
            fail=$((fail+1))
        fi
    else
        echo "  SKIP pkg-smoke (missing ${PKGR} 或夹具 ${TMP}/game)"
    fi
    grep_step "guid-chain smoke (insert+rename+manifest-wipe -> reopen per-entity resolve; M6a-b0)" \
        "smoke-guid: .* => OK" \
        "${EDITOR}" --smoke-guid --no-reopen
    # M6c 批③：音频全链（夹具自播种 wav → 导入/meta/后台烤/Peek/试听 +
    # AudioSource playOnStart 逻辑声部断言）。LEMON_AUDIO=off 强制静音 = 无头
    # 确定性口径（"off 无头环境同绿"判据的机器面；设备路径 = 真人/svr-test 验）
    grep_step "audio-chain smoke (seeded wav import/bake/preview + playOnStart voices; LEMON_AUDIO=off; M6c-b3)" \
        "smoke-audio.*OK" \
        env LEMON_AUDIO=off "${EDITOR}" --project "${TMP}/audio" --smoke-audio --no-reopen
    if [ -f "${SDK}" ]; then
        grep_step "uirml-chain smoke (RmlUi C# API: ops/clone/click-events/contract + font/asset/hot-reload + uidoc dual-channel/stale/layer/watcher-evict + zombie-render triple-form + reconcile + entity-delete evict3; M6b-b3d)" \
            "smoke-uirml: .* => OK" \
            "${EDITOR}" --script "${SDK}" --smoke-uirml --frames 470 --validate --no-reopen
    else
        echo "  SKIP uirml-chain (missing ${SDK})"
    fi
    # 形态 D 画面防线（2026-09-29）：无脚本模式——第四局（声明实体已删）gameRT
    # 像素断言；脚本模式 C# 帧 1 合法拉回，pix 位不裁决
    grep_step "uirml-noscript smoke (entity-delete evict3 pixel guard; M6b-b3d)" \
        "smoke-uirml: .* => OK" \
        "${EDITOR}" --smoke-uirml --frames 470 --validate --no-reopen
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
