#!/bin/bash
# Clear Skies for Linux as one file: dist/ClearSkies-<version>-x86_64.AppImage, with the launcher window
# (src/launcher_sdl.c, SDL3 linked in), the layer, its manifest, the default clearskies.ini, the fonts and licenses.
#
# Run it on Ubuntu 22.04 (glibc 2.35, the oldest Savage Wind supports) with the packages the "appimage" job of
# .github/workflows/linux.yml installs. Downloads, each checked against its SHA-256: SDL 3.4.16 (the version Savage
# Wind builds with), and appimagetool 1.9.1 with the type2 runtime (the versions Savage Wind's own AppImage uses).
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^#define APP_VERSION "\(.*\)"/\1/p' src/launcher_sdl.c)
W=$PWD/build/appimage
mkdir -p "$W/src" dist

fetch() {  # url sha256 file
    [ -f "$W/src/$3" ] || { curl -fsSL -o "$W/src/$3.part" "$1" && mv "$W/src/$3.part" "$W/src/$3"; }
    echo "$2  $W/src/$3" | sha256sum -c --quiet
}
fetch https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz \
      7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68 SDL3-3.4.16.tar.gz
fetch https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage \
      ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 appimagetool-x86_64.AppImage
fetch https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 \
      2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d runtime-x86_64

# SDL3, static: video only (X11 and Wayland, both loaded at run time), with the file dialog (desktop portal or zenity)
if [ ! -f "$W/sdl/lib/libSDL3.a" ]; then
    rm -rf "$W/sdl-src" && mkdir "$W/sdl-src"
    tar xzf "$W/src/SDL3-3.4.16.tar.gz" -C "$W/sdl-src" --strip-components=1
    cmake -S "$W/sdl-src" -B "$W/sdl-build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$W/sdl" \
        -DCMAKE_INSTALL_LIBDIR=lib -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
        -DSDL_AUDIO=OFF -DSDL_CAMERA=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_SENSOR=OFF -DSDL_HIDAPI=OFF \
        -DSDL_POWER=OFF -DSDL_GPU=OFF -DSDL_X11=ON -DSDL_WAYLAND=ON -DSDL_DBUS=ON
    cmake --build "$W/sdl-build"
    cmake --install "$W/sdl-build"
fi

command -v python3 >/dev/null && python3 -I tools/gen_presets.py

CFLAGS="-O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers"
A=$W/AppDir
D=$A/usr/share/clearskies
rm -rf "$A" && mkdir -p "$A/usr/bin" "$D/fonts"
# shellcheck disable=SC2086,SC2046
cc $CFLAGS -o "$A/usr/bin/clearskies" src/launcher_sdl.c \
    $(PKG_CONFIG_PATH="$W/sdl/lib/pkgconfig" pkg-config --static --cflags --libs sdl3) -s
# (the layer as build.sh builds it: only the loader's entry points exported)
# shellcheck disable=SC2086
cc $CFLAGS -fPIC -shared -fvisibility=hidden -Wl,-Bsymbolic -Wl,-soname,libclearskies_layer.so \
    -o "$D/libclearskies_layer.so" src/clearskies_layer.c -ldl -lpthread -s
sed 's|"\.\\\\clearskies_layer\.dll"|"./libclearskies_layer.so"|' VkLayer_clearskies.json >"$D/VkLayer_clearskies.json"
grep -q '"./libclearskies_layer.so"' "$D/VkLayer_clearskies.json"
cp clearskies.ini LICENSE LICENSES.txt README.md "$D/"
cp docs/icon.png "$D/clearskies.png"
cp third_party/fonts/FiraSans/FiraSans-Medium.ttf third_party/fonts/FiraSans/FiraSans-SemiBold.ttf \
   third_party/fonts/FiraSans/FiraSans-OFL.txt "$D/fonts/"
cp tools/linux/AppRun "$A/AppRun" && chmod +x "$A/AppRun"
cp tools/linux/clearskies.desktop "$A/"
cp docs/icon.png "$A/clearskies.png" && ln -sf clearskies.png "$A/.DirIcon"

echo "launcher needs:"; ldd "$A/usr/bin/clearskies" | sed 's/^/    /'
OUT=dist/ClearSkies-$VERSION-x86_64.AppImage
chmod +x "$W/src/appimagetool-x86_64.AppImage"
rm -f "$OUT"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$W/src/appimagetool-x86_64.AppImage" --no-appstream \
    --runtime-file "$W/src/runtime-x86_64" "$A" "$OUT"
ls -l "$OUT"
sha256sum "$OUT"
