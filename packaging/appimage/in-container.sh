#!/bin/bash
# Runs inside the build container: builds the client, assembles the AppDir and packs the AppImage.
# Expects the repository at /src (read-only), the version in $VERSION and an output directory /out.
set -euo pipefail

VERSION="${VERSION:?VERSION must be set}"
set +u    # scl_source reads variables it never sets
source scl_source enable gcc-toolset-12
set -u
export PKG_CONFIG_PATH=/opt/raylib/lib64/pkgconfig:/opt/raylib/lib/pkgconfig:/opt/ffmpeg/lib/pkgconfig

WORK=/tmp/work
rm -rf "$WORK" && mkdir "$WORK" && cp -a /src/. "$WORK/" && cd "$WORK"
rm -rf bin build

# The static raylib needs the system libraries named in LDLIBS after it on the link line.
# The libstdc++ and libgcc runtime are linked in; the FFmpeg libraries are found next to the
# executable through DT_RPATH (not RUNPATH, which is not searched for their dependencies).
make -j"$(nproc)" bin/tvt-dvr-client bin/devicecheck \
    PKG_CONFIG_FLAGS=--static \
    CXXFLAGS="-O2 -static-libstdc++ -static-libgcc" \
    LDFLAGS="-Wl,--disable-new-dtags -Wl,-rpath,'\$\$ORIGIN/../lib'" \
    LDLIBS="-lX11 -ldl -lpthread -lm -lrt"

APPDIR=/tmp/AppDir
rm -rf "$APPDIR" && mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/share/doc/tvt-dvr-client"
cp bin/tvt-dvr-client "$APPDIR/usr/bin/"
cp -a /opt/ffmpeg/lib/libavcodec.so* /opt/ffmpeg/lib/libavformat.so* /opt/ffmpeg/lib/libavutil.so* \
      /opt/ffmpeg/lib/libswscale.so* "$APPDIR/usr/lib/"
strip --strip-unneeded "$APPDIR/usr/bin/tvt-dvr-client" "$APPDIR"/usr/lib/*.so.*.*.* 2>/dev/null || true

PKG=packaging/appimage
install -m 755 "$PKG/AppRun" "$APPDIR/AppRun"
cp "$PKG/tvt-dvr-client.desktop" "$PKG/tvt-dvr-client.svg" "$APPDIR/"
ln -s tvt-dvr-client.svg "$APPDIR/.DirIcon"
DOC="$APPDIR/usr/share/doc/tvt-dvr-client"
cp LICENSE "$DOC/LICENSE"
cp "$PKG/THIRD-PARTY-NOTICES.md" "$DOC/"
cp /opt/src/ffmpeg/COPYING.LGPLv2.1 "$DOC/ffmpeg-COPYING.LGPLv2.1"
cp /opt/src/raylib/LICENSE "$DOC/raylib-LICENSE"
# FFmpeg embeds the options it was configured with.
strings -a "$APPDIR/usr/lib/libavcodec.so" | grep -m1 -- '^--prefix' > "$DOC/ffmpeg-configure.txt"
[ -s "$DOC/ffmpeg-configure.txt" ] || { echo "cannot read the FFmpeg configuration"; exit 1; }
desktop-file-validate "$APPDIR/tvt-dvr-client.desktop"

# What the bundle needs from the host: everything but the allow-listed system libraries must be inside.
echo "== libraries resolved outside the AppDir"
export LD_LIBRARY_PATH="$APPDIR/usr/lib"
outside=$(for f in "$APPDIR/usr/bin/tvt-dvr-client" "$APPDIR"/usr/lib/*.so.*.*.*; do ldd "$f"; done \
          | awk '/=>/ && $3 !~ /AppDir/ {print $1}' | sort -u)
echo "$outside" | tr '\n' ' '; echo
unexpected=$(echo "$outside" | grep -vE '^(libc|libm|libdl|libpthread|librt|libutil|libX11|libxcb|libXau|libXdmcp|libGL|libGLdispatch|libGLX|libXext|libXrandr|libXi|libXcursor|libXinerama|libXrender|libXfixes|libz)\.so' || true)
[ -z "$unexpected" ] || { echo "UNEXPECTED host dependencies: $unexpected"; exit 1; }
unset LD_LIBRARY_PATH
echo "== highest GLIBC symbol version required"
MAX_GLIBC=GLIBC_2.28
highest=$(objdump -T "$APPDIR/usr/bin/tvt-dvr-client" "$APPDIR"/usr/lib/*.so.*.*.* 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
echo "$highest (limit $MAX_GLIBC)"
[ "$(printf '%s\n' "$highest" "$MAX_GLIBC" | sort -V | tail -1)" = "$MAX_GLIBC" ] || { echo "the bundle needs a newer glibc than $MAX_GLIBC"; exit 1; }
echo "== RPATH of the executable"; readelf -d "$APPDIR/usr/bin/tvt-dvr-client" | grep -E 'RPATH|RUNPATH'

OUT="/out/tvt-dvr-client-${VERSION}-x86_64.AppImage"
rm -f "$OUT"
ARCH=x86_64 /opt/appimagetool --appimage-extract-and-run --runtime-file /opt/runtime-x86_64 "$APPDIR" "$OUT"
cp bin/devicecheck /out/devicecheck-slim-ffmpeg
chmod 755 "$OUT"
( cd /out && sha256sum "$(basename "$OUT")" > "$(basename "$OUT").sha256" )
ls -la /out
