# Third-party software in this AppImage

This AppImage contains the following software besides tvt-dvr-client itself (MIT, see LICENSE).

## FFmpeg 7.1.5 (LGPL-2.1-or-later)

The shared libraries `libavcodec`, `libavformat`, `libavutil` and `libswscale` in `usr/lib` come
from the unmodified FFmpeg 7.1.5 source release, built without `--enable-gpl` and
`--enable-nonfree`. The options used are in `ffmpeg-configure.txt`; the licence text is in
`ffmpeg-COPYING.LGPLv2.1`.

The source is available from <https://ffmpeg.org/releases/ffmpeg-7.1.5.tar.xz> and is attached to
the release that carries this AppImage. The libraries are linked dynamically, so they can be
replaced with a build of your own: put the files in `usr/lib` of the extracted AppImage
(`--appimage-extract`) and repack it, or run the client with `LD_LIBRARY_PATH` pointing to them.

## raylib 6.2 (zlib/libpng licence)

raylib is linked statically, together with the libraries it embeds (GLFW, miniaudio, the stb
headers), which are under the zlib licence or in the public domain. The licence text is in
`raylib-LICENSE`; the source is at <https://github.com/raysan5/raylib/tree/6.2>.

## System libraries

The AppImage relies on the host for the C library, the X11 client libraries and OpenGL. Nothing
of the DVR manufacturer's software is included.
