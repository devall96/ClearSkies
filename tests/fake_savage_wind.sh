#!/bin/sh
# NOT Savage Wind: a fake stand-in used only by tests/appimage_test.sh. Clear Skies' "Play" starts it the way it
# starts the real Savage Wind AppImage; it checks that the layer was turned on for it, then runs the layer test
# (build/appimage-test/layer_test) in its place, which creates a Vulkan device and a shader the way the game does.
set -eu
case ":${VK_INSTANCE_LAYERS:-}:" in *:VK_LAYER_CLEARSKIES_savage_wind:*) ;; *) echo "fake game: layer not on"; exit 3 ;; esac
[ -n "${CLEARSKIES_INI:-}" ] || { echo "fake game: CLEARSKIES_INI not set"; exit 3; }
[ -z "${APPDIR:-}" ] || { echo "fake game: Clear Skies' APPDIR leaked in"; exit 3; }
echo "fake game: started by Clear Skies with the layer on, in $(pwd)"
exec "$(dirname "$0")/layer_test" "$@"
