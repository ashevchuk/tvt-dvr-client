# Build environment for the AppImage. AlmaLinux 8 keeps the glibc requirement at 2.28.
# FFmpeg is built here with a minimal LGPL configuration and raylib as a static library, so the
# AppImage carries only what the client uses.
FROM almalinux:8

SHELL ["/bin/bash", "-c"]

RUN dnf -y install dnf-plugins-core \
 && dnf config-manager --set-enabled powertools \
 && dnf -y --setopt=install_weak_deps=False install \
        gcc-toolset-12-gcc-c++ gcc-toolset-12-binutils make cmake nasm pkgconf-pkg-config \
        xz tar gzip file diffutils findutils which curl ca-certificates squashfs-tools desktop-file-utils \
        libX11-devel libXrandr-devel libXi-devel libXcursor-devel libXinerama-devel mesa-libGL-devel \
 && dnf clean all

ARG FFMPEG_VERSION=7.1.5
ARG FFMPEG_SHA256=de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f
ARG RAYLIB_VERSION=6.2
ARG RAYLIB_SHA256=df7a13129b1bec9e73a6962efff170beba310ee6eab8f94cd000b7316b4db5ee
ARG APPIMAGETOOL_VERSION=1.9.1
ARG APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
ARG RUNTIME_VERSION=20251108
ARG RUNTIME_SHA256=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d

# FFmpeg: decoders for H.264 and IMA ADPCM, the Matroska muxer and the scaler; nothing else.
# No --enable-gpl or --enable-nonfree, so the libraries stay LGPL and are bundled as shared objects.
RUN mkdir -p /opt/src \
 && curl -fsSL -o /opt/src/ffmpeg.tar.xz "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz" \
 && echo "${FFMPEG_SHA256}  /opt/src/ffmpeg.tar.xz" | sha256sum -c - \
 && tar -C /opt/src --no-same-owner -xf /opt/src/ffmpeg.tar.xz \
 && chmod -R a+rX "/opt/src/ffmpeg-${FFMPEG_VERSION}" \
 && rm /opt/src/ffmpeg.tar.xz \
 && cd "/opt/src/ffmpeg-${FFMPEG_VERSION}" \
 && source scl_source enable gcc-toolset-12 \
 && ./configure --prefix=/opt/ffmpeg --enable-shared --disable-static --disable-programs --disable-doc \
        --disable-debug --disable-autodetect --disable-network --disable-everything \
        --disable-avdevice --disable-avfilter --disable-swresample --disable-postproc \
        --enable-swscale \
        --enable-decoder=h264,adpcm_ima_wav --enable-parser=h264 \
        --enable-muxer=matroska --enable-protocol=file \
 && make -j"$(nproc)" && make install \
 && ln -s "/opt/src/ffmpeg-${FFMPEG_VERSION}" /opt/src/ffmpeg

RUN curl -fsSL -o /opt/src/raylib.tar.gz "https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz" \
 && echo "${RAYLIB_SHA256}  /opt/src/raylib.tar.gz" | sha256sum -c - \
 && tar -C /opt/src --no-same-owner -xzf /opt/src/raylib.tar.gz \
 && chmod -R a+rX "/opt/src/raylib-${RAYLIB_VERSION}" \
 && rm /opt/src/raylib.tar.gz \
 && ln -s "/opt/src/raylib-${RAYLIB_VERSION}" /opt/src/raylib \
 && source scl_source enable gcc-toolset-12 \
 && cmake -S /opt/src/raylib -B /tmp/raylib-build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX=/opt/raylib \
 && cmake --build /tmp/raylib-build -j"$(nproc)" && cmake --install /tmp/raylib-build \
 && rm -rf /tmp/raylib-build

RUN curl -fsSL -o /opt/appimagetool "https://github.com/AppImage/appimagetool/releases/download/${APPIMAGETOOL_VERSION}/appimagetool-x86_64.AppImage" \
 && echo "${APPIMAGETOOL_SHA256}  /opt/appimagetool" | sha256sum -c - \
 && curl -fsSL -o /opt/runtime-x86_64 "https://github.com/AppImage/type2-runtime/releases/download/${RUNTIME_VERSION}/runtime-x86_64" \
 && echo "${RUNTIME_SHA256}  /opt/runtime-x86_64" | sha256sum -c - \
 && chmod +x /opt/appimagetool
