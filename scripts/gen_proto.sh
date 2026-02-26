#!/usr/bin/env bash
# gen_proto.sh — Fetch Meshtastic .proto files and generate nanopb C sources.
#
# Prerequisites:
#   pip install nanopb grpcio-tools
#   brew install protobuf   (macOS)  or  apt install -y protobuf-compiler (Debian)
#
# Run once before your first build, and again whenever you want to update to a
# newer Meshtastic firmware proto schema.
#
# Output is written to:
#   components/meshtastic_ble/proto/
#   components/meshtastic_ble/nanopb/   (nanopb runtime library files)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── Config ────────────────────────────────────────────────────────────────────
# Tag or branch to fetch from the Meshtastic protobufs repository.
MESHTASTIC_PROTO_VERSION="${MESHTASTIC_PROTO_VERSION:-v2.5}"

# Pinned nanopb version — keep in sync with what esp-idf ships, or use a
# standalone install.
NANOPB_VERSION="${NANOPB_VERSION:-0.4.8}"

PROTO_SRC_DIR="$REPO_ROOT/.proto_src"
PROTO_OUT_DIR="$REPO_ROOT/components/meshtastic_ble/proto"
NANOPB_DIR="$REPO_ROOT/components/meshtastic_ble/nanopb"

# ── Fetch Meshtastic protobufs ────────────────────────────────────────────────
echo "==> Fetching Meshtastic protobufs (${MESHTASTIC_PROTO_VERSION})..."
mkdir -p "$PROTO_SRC_DIR"
curl -sSL \
    "https://github.com/meshtastic/protobufs/archive/refs/tags/${MESHTASTIC_PROTO_VERSION}.tar.gz" \
    | tar -xz --strip-components=1 -C "$PROTO_SRC_DIR"

# ── Fetch nanopb ──────────────────────────────────────────────────────────────
echo "==> Fetching nanopb runtime (${NANOPB_VERSION})..."
mkdir -p "$NANOPB_DIR"
TMP_NANOPB="$(mktemp -d)"
curl -sSL \
    "https://github.com/nanopb/nanopb/archive/refs/tags/${NANOPB_VERSION}.tar.gz" \
    | tar -xz --strip-components=1 -C "$TMP_NANOPB"

# Only the runtime files are needed inside the component; the generator is used
# here but not shipped.
cp "$TMP_NANOPB/pb.h" \
   "$TMP_NANOPB/pb_common.h" "$TMP_NANOPB/pb_common.c" \
   "$TMP_NANOPB/pb_decode.h" "$TMP_NANOPB/pb_decode.c" \
   "$TMP_NANOPB/pb_encode.h" "$TMP_NANOPB/pb_encode.c" \
   "$NANOPB_DIR/"

NANOPB_GENERATOR="$TMP_NANOPB/generator/nanopb_generator.py"

# ── Generate C sources ────────────────────────────────────────────────────────
echo "==> Generating nanopb sources..."
mkdir -p "$PROTO_OUT_DIR/meshtastic"

# The .proto files we need for the BLE API.
PROTOS=(
    mesh
    telemetry
    config
    channel
    deviceonly
    portnums
    xmodem
)

for proto in "${PROTOS[@]}"; do
    src="$PROTO_SRC_DIR/meshtastic/${proto}.proto"
    if [[ ! -f "$src" ]]; then
        echo "  WARN: ${proto}.proto not found in downloaded archive, skipping"
        continue
    fi
    echo "  generating ${proto}.pb.{h,c}"
    python3 "$NANOPB_GENERATOR" \
        --proto-path="$PROTO_SRC_DIR" \
        --output-dir="$PROTO_OUT_DIR" \
        "$src"
done

# ── Cleanup ───────────────────────────────────────────────────────────────────
rm -rf "$TMP_NANOPB" "$PROTO_SRC_DIR"

echo ""
echo "Done.  Generated files are in:"
echo "  $PROTO_OUT_DIR"
echo "  $NANOPB_DIR"
echo ""
echo "Commit these generated files to source control so builds are reproducible"
echo "without requiring protoc on the build machine."
