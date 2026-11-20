#!/usr/bin/env bash
# Native Linux build, including ARM64. Versions and patch provenance are pinned.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIBPEER_DIR="${LIBPEER_DIR:-${OUTROOT:-$HOME/orbita-webrtc}/upstream/libpeer}"
REF=9319aa434cb9e893faed0293ba9d2a21eca59c8b
if [ ! -e "$LIBPEER_DIR/.git" ]; then
    git init -q "$LIBPEER_DIR"
    git -C "$LIBPEER_DIR" remote add origin https://github.com/sepfy/libpeer.git
    git -C "$LIBPEER_DIR" fetch --depth 1 origin "$REF"
    git -C "$LIBPEER_DIR" checkout -q FETCH_HEAD
    git -C "$LIBPEER_DIR" submodule update --init --depth 1 \
        third_party/mbedtls third_party/usrsctp third_party/libsrtp
fi
[ "$(git -C "$LIBPEER_DIR" rev-parse HEAD)" = "$REF" ] || { echo 'Unexpected libpeer revision'; exit 1; }
if ! git -C "$LIBPEER_DIR" apply --reverse --check --include='src/*' "$HERE/libpeer-green-nx.patch" 2>/dev/null; then
    git -C "$LIBPEER_DIR" apply --include='src/*' "$HERE/libpeer-green-nx.patch"
fi
export LIBPEER_DIR OUTROOT="${OUTROOT:-$HOME/orbita-webrtc}"
# A Windows checkout may contain CRLF in the older dependency build scripts.
TOOLS="$OUTROOT/build-tools/webrtc-xenon"
mkdir -p "$TOOLS"
cp -r "$HERE/../webrtc-xenon/." "$TOOLS/"
sed -i 's/\r$//' "$TOOLS/build.sh"
bash "$TOOLS/build.sh" host
mkdir -p "$OUTROOT/host/licenses"
cp "$LIBPEER_DIR/LICENSE" "$OUTROOT/host/licenses/libpeer-LICENSE"
cp "$LIBPEER_DIR/third_party/mbedtls/LICENSE" "$OUTROOT/host/licenses/mbedTLS-LICENSE"
cp "$LIBPEER_DIR/third_party/libsrtp/LICENSE" "$OUTROOT/host/licenses/libsrtp-LICENSE"
cp "$LIBPEER_DIR/third_party/usrsctp/LICENSE.md" "$OUTROOT/host/licenses/usrsctp-LICENSE"
