#!/bin/sh
# Clear Skies' AppImage, tested the way players use it:
#   tests/appimage_test.sh dist/ClearSkies-<version>-x86_64.AppImage
# 1. the command line (--help, --list, --preset ... --no-play), with settings in ~/.config/ClearSkies
# 2. the window: drawn to a PNG (build/appimage-test/*.png), and opened on X (xvfb-run, when there is one)
# 3. Steam's launch options (ClearSkies.AppImage %command%): every preset, through the layer inside the AppImage
# 4. Play: --play starts a FAKE game (tests/fake_savage_wind.sh, not Savage Wind) with the layer on
# Needs a Vulkan driver (Mesa's lavapipe is enough), the system's shaderc and a C compiler for the layer test.
set -eu
APP=$(readlink -f "$1")
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd -P)
WORK=$ROOT/build/appimage-test
rm -rf "$WORK" && mkdir -p "$WORK/home" "$WORK/sw-data/extracted/romfs"
cc -O2 -o "$WORK/layer_test" "$ROOT/tests/layer_test.c" -ldl
cp "$ROOT/tests/fake_savage_wind.sh" "$WORK/" && chmod +x "$WORK/fake_savage_wind.sh"
export HOME="$WORK/home" APPIMAGE_EXTRACT_AND_RUN=1 SAVAGEWIND_DATA_ROOT="$WORK/sw-data"
unset XDG_CONFIG_HOME
CONF=$HOME/.config/ClearSkies
fail() { echo "FAIL: $*"; exit 1; }

echo "== command line"
"$APP" --help | grep -q 'Savage Wind' || fail "--help"
[ "$("$APP" --list | wc -l)" = 21 ] || fail "--list should show 21 looks"
"$APP" --preset filmic --no-play >/dev/null || fail "--preset filmic --no-play"
grep -q '^preset = filmic' "$CONF/clearskies.ini" || fail "the preset wasn't saved to $CONF/clearskies.ini"
grep -q '^;   cobalt ' "$CONF/clearskies.ini" || fail "the first run didn't start from the shipped clearskies.ini"
if "$APP" --preset nonsense --no-play 2>/dev/null; then fail "an unknown preset was accepted"; fi
echo ok

echo "== the window"
"$APP" --screenshot "$WORK/window.png" || fail "--screenshot"
"$APP" --screenshot "$WORK/window-list.png" --open-list || fail "--screenshot --open-list"
if command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a "$APP" --smoke || fail "the window didn't open on X"
fi
echo ok

echo "== Steam's launch options, every preset"
failed=0 count=0
for p in off $("$APP" --list | awk '$1 != "off" { print $1 }'); do
    count=$((count + 1))
    "$APP" --preset "$p" --no-play >/dev/null
    rm -f "$CONF/clearskies.log"
    if out=$("$APP" "$WORK/layer_test" 2>&1); then code=0; else code=$?; fi
    log=$(cat "$CONF/clearskies.log" 2>/dev/null || true)
    replaced=$(printf '%s\n' "$log" | grep -c 'replaces the tone-mapping composite' || true)
    ok=0
    if [ "$code" = 0 ] && printf '%s' "$log" | grep -q 'loaded into'; then
        if [ "$p" = off ]; then [ "$replaced" = 0 ] && ok=1
        else [ "$replaced" = 1 ] && printf '%s' "$log" | grep -q 'compiled' && ok=1; fi
    fi
    if [ "$ok" = 1 ]; then printf '%-20s ok\n' "$p"
    else failed=$((failed + 1)); printf '%-20s FAIL\n%s\n%s\n' "$p" "$out" "$log"; fi
done
[ "$failed" = 0 ] || fail "$failed of $count presets"
echo "all $count presets ok"

echo "== Play (a fake game, not Savage Wind)"
"$APP" --preset serfrost --game "$WORK/fake_savage_wind.sh" --no-play >/dev/null
rm -f "$CONF/clearskies.log"
"$APP" --play || fail "--play"
grep -q 'replaces the tone-mapping composite' "$CONF/clearskies.log" || fail "--play: Clarity wasn't swapped in"
echo ok
echo "all AppImage tests passed"
