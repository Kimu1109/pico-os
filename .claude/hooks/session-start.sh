#!/bin/bash
# Claude Code on the web 用の SessionStart hook。
#
# pico-os は実機(PlatformIO)とPC/ネイティブ(CMake + SDL2)の2系統のビルドを持つ
# (詳細は CLAUDE.md「ビルド構成」「PC / Web実行環境」参照)。ここでは両方が
# このセッション内ですぐ叩ける状態まで用意する:
#   - PlatformIO CLI 本体の導入
#   - 実機ビルド(env:rpipico2w)が使うプラットフォーム/ライブラリの取得
#     (platformio.ini の lib_deps が LovyanGFX の版の唯一の情報源)
#   - SDL2開発パッケージの導入と、PCネイティブビルドの configure
#     (pc/CMakeLists.txt が同じ platformio.ini を読んで LovyanGFX の版を揃える)
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

export DEBIAN_FRONTEND=noninteractive

# --- OSパッケージ: SDL2開発ヘッダ + ネイティブビルド一式 + PlatformIO導入用のpipx ---
# (cmake/g++/ninjaはベースイメージに既に入っている前提だが、無い環境でも動くよう含めておく)
apt-get update -qq
apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build \
    libsdl2-dev \
    pipx

# --- PlatformIO CLI (実機ビルド env:rpipico2w 用) ---
# Ubuntu 24.04は python3-pip が externally-managed のため pipx 経由で入れる。
# 既に入っていれば何もしない(コンテナ再利用時にネットワークへ行かせないため)。
export PIPX_HOME="${PIPX_HOME:-$HOME/.local/pipx}"
export PIPX_BIN_DIR="${PIPX_BIN_DIR:-$HOME/.local/bin}"
if ! command -v pio >/dev/null 2>&1; then
  pipx install platformio
fi
export PATH="$PIPX_BIN_DIR:$PATH"
if [ -n "${CLAUDE_ENV_FILE:-}" ]; then
  echo "export PATH=\"$PIPX_BIN_DIR:\$PATH\"" >> "$CLAUDE_ENV_FILE"
fi

# --- 実機ビルドの依存(プラットフォームパッケージ/ライブラリ)を先取りダウンロード ---
# platformio.ini の lib_deps は完全固定なので、ここで一度取得すればセッション中は
# 再ダウンロードが起きない。失敗してもPlatformIO CLI自体は使える状態なので
# セッション開始は止めず、警告だけ出して先へ進む(レジストリ側の一時的な問題や
# ボード定義の不整合をここでブロッキングにしない)。
if ! pio pkg install --project-dir "$CLAUDE_PROJECT_DIR" -e rpipico2w; then
  echo "[session-start] 警告: 'pio pkg install -e rpipico2w' に失敗しました。" >&2
  echo "[session-start] PlatformIO CLI 自体は使えるので、実機ビルド時に 'pio run -e rpipico2w' で改めて確認してください。" >&2
fi

# --- PCネイティブビルドのconfigureを通しておく(LovyanGFXの取得も含む) ---
# platformio.ini から読んだ版のLovyanGFXをFetchContentで取ってくるところまでを
# ここで済ませておけば、セッション中の初回ビルドがネットワーク待ちにならない。
cmake -S "$CLAUDE_PROJECT_DIR/pc" -B "$CLAUDE_PROJECT_DIR/pc/build" -G Ninja
