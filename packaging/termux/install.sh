#!/data/data/com.termux/files/usr/bin/bash
# VoxForgeSynth-101 — Termux 安装脚本
# 用法： bash install.sh
set -e

echo "== VoxForgeSynth-101 Termux 安装 =="

# 1. 依赖
command -v clang >/dev/null || { echo "安装 clang…"; pkg install -y clang; }
command -v make  >/dev/null || { echo "安装 make…";  pkg install -y make; }
pkg install -y luajit 2>/dev/null || true

# 2. 编译
SRC="${1:-$(pwd)}"
cd "$SRC"
echo "编译 kt_lua …"
clang++ -O2 -std=c++17 -Isrc \
    -Ithird_party/klatt-tts/include \
    -Ithird_party/klatt-tts/include/tts \
    -Ithird_party/klatt-tts/include/g2p \
    -Ithird_party/tsf \
    -I$PREFIX/include/luajit-2.1 \
    -o kt_lua tools/kt_lua.cpp third_party/klatt-tts/src/*.cc \
    -lluajit-5.1 -lm

# 3. 安装
mkdir -p "$PREFIX/share/voxforge/scripts"
cp kt_lua "$PREFIX/bin/voxforge"
cp -r scripts/* "$PREFIX/share/voxforge/scripts/" 2>/dev/null || true
cp data/g2p_char.txt data/g2p_phrase.txt "$PREFIX/share/voxforge/" 2>/dev/null || true

echo "安装完成： voxforge <script.lua>"
echo "示例：    voxforge $PREFIX/share/voxforge/scripts/demo.lua"
