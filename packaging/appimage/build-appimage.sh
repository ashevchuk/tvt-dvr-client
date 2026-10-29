#!/bin/bash
# Builds the AppImage in a container. Usage: packaging/appimage/build-appimage.sh VERSION [OUTPUT_DIR]
# The result is OUTPUT_DIR/tvt-dvr-client-VERSION-x86_64.AppImage with a .sha256 next to it.
set -euo pipefail

VERSION="${1:?usage: $0 VERSION [OUTPUT_DIR]}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="$(mkdir -p "${2:-$REPO/dist}" && cd "${2:-$REPO/dist}" && pwd)"
IMAGE=tvt-dvr-appimage-builder:1

# Build from the committed tree so that the file matches the commit a release tag points to.
if [ -n "$(git -C "$REPO" status --porcelain)" ]; then
    echo "the working tree has uncommitted changes; commit them first" >&2
    exit 1
fi
SRC="$(mktemp -d)"
trap 'rm -rf "$SRC"' EXIT
git -C "$REPO" archive HEAD | tar -x -C "$SRC"
echo "building commit $(git -C "$REPO" rev-parse --short HEAD) as version $VERSION"

docker build -t "$IMAGE" "$HERE"
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -e VERSION="$VERSION" -v "$SRC":/src:ro -v "$OUT":/out "$IMAGE" /src/packaging/appimage/in-container.sh
