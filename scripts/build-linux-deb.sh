#!/usr/bin/env bash
set -euo pipefail

version="0.3.0"
root="$PWD/package-root"
output="$PWD/installer-output"

cmake -S native-linux -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --parallel

mkdir -p "$root/DEBIAN" "$root/usr/bin" "$root/usr/share/applications" \
  "$root/usr/share/icons/hicolor/scalable/apps" "$output"
DESTDIR="$root" cmake --install build-linux --prefix /usr
install -m 0644 native-linux/packaging/control "$root/DEBIAN/control"
install -m 0644 native-linux/packaging/screenpilot.desktop \
  "$root/usr/share/applications/screenpilot.desktop"
install -m 0644 assets/screenpilot.svg \
  "$root/usr/share/icons/hicolor/scalable/apps/screenpilot.svg"

dpkg-deb --root-owner-group --build "$root" \
  "$output/screenpilot_${version}_amd64.deb"
