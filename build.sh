#!/bin/sh
set -eu
cd "$(dirname "$0")"

CC=${CC:-gcc}

pkg-config --exists sdl3 freetype2 || {
    echo "error: sdl3 and freetype2 development packages required" >&2
    exit 1
}

${CC} -g ${CFLAGS:-} -o quickpick \
    src/quickpick.c \
    src/util/autil.c src/util/math.c src/util/string.c src/util/containers.c \
    src/util/window_sdl.c src/util/window_common.c src/util/draw_opengl.c \
    src/util/draw_common.c src/util/tlsf.c src/util/glad.c \
    src/util/platform_unix.c \
    -Iinclude -Isrc/util -Ires \
    $(pkg-config --libs --cflags freetype2) $(pkg-config --libs --cflags sdl3) \
    -lm ${LDFLAGS:-} "$@"
