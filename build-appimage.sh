#!/bin/sh
set -e

cd "$(dirname "$0")"

if [ "$1" != "--inside" ]; then
    if command -v podman >/dev/null 2>&1; then
        containerTool=podman
    elif command -v docker >/dev/null 2>&1; then
        containerTool=docker
    else
        echo "You need podman or docker to build the AppImage"
        exit 1
    fi

    $containerTool run --rm -v "$PWD":/work:Z -w /work ubuntu:20.04 sh /work/build-appimage.sh --inside
    echo "Done: $(ls ./*.AppImage)"
    exit 0
fi

export DEBIAN_FRONTEND=noninteractive
export APPIMAGE_EXTRACT_AND_RUN=1

apt-get update
apt-get install -y --no-install-recommends ca-certificates wget file g++ make cmake pkg-config libgtk-3-dev librsvg2-common libglib2.0-bin dpkg-dev

rm -rf appimage-build AppDir
cmake -B appimage-build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DSTATIC_CPP=ON
cmake --build appimage-build -j"$(nproc)"
DESTDIR="$PWD/AppDir" cmake --install appimage-build

mkdir -p tools
if [ ! -f tools/linuxdeploy ]; then
    wget -q -O tools/linuxdeploy https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    wget -q -O tools/linuxdeploy-plugin-gtk.sh https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh
    chmod +x tools/linuxdeploy tools/linuxdeploy-plugin-gtk.sh
fi

export PATH="$PWD/tools:$PATH"
export DEPLOY_GTK_VERSION=3
export OUTPUT=Borderlands_Online_Launcher-x86_64.AppImage

linuxdeploy --appdir AppDir \
    --desktop-file packaging/bol-launcher.desktop \
    --icon-file packaging/bol-launcher.png \
    --plugin gtk \
    --output appimage

rm -rf AppDir appimage-build
