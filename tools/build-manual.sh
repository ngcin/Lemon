#!/bin/sh
# build-manual.sh — 手册站一行包装（M8 批⑦ A4）；产物 build/manual/，file:// 直开
cd "$(dirname "$0")/.." || exit 1
exec node tools/build-manual.mjs "$@"
