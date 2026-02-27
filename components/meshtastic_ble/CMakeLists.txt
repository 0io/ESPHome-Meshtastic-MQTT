# CMakeLists.txt — meshtastic_ble ESPHome external component
#
# Consumed by the esp-idf build system when ESPHome adds this directory to
# EXTRA_COMPONENT_DIRS.  Registers the component's sources and adds the nanopb
# runtime and generated proto directories to the compiler include path.
#
# Prerequisites (one-time, on the host machine):
#   bash scripts/gen_proto.sh
# This populates:
#   nanopb/   — nanopb runtime headers and .c files
#   proto/    — generated meshtastic .pb.h / .pb.c files

cmake_minimum_required(VERSION 3.16)

# ── nanopb runtime ────────────────────────────────────────────────────────────
set(NANOPB_SRCS
    nanopb/pb_common.c
    nanopb/pb_decode.c
    nanopb/pb_encode.c
)

# ── Generated Meshtastic protobuf sources ─────────────────────────────────────
file(GLOB PROTO_SRCS "proto/meshtastic/*.pb.c")

# ── Component registration ────────────────────────────────────────────────────
idf_component_register(
    SRCS
        "meshtastic_ble.cpp"
        ${NANOPB_SRCS}
        ${PROTO_SRCS}
    INCLUDE_DIRS
        "."         # meshtastic_ble.h, gatt_defs.h, ble_uuids.h
        "nanopb"    # pb.h, pb_decode.h, pb_encode.h
        "proto"     # generated headers included as "meshtastic/mesh.pb.h"
    REQUIRES
        bt          # NimBLE + Bluetooth controller
        nvs_flash   # required by NimBLE for key storage
)
