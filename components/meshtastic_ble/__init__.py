"""
ESPHome custom component: meshtastic_ble

Registers the MeshtasticBLEComponent with the ESPHome code-generation
pipeline.  This file is the bridge between meshtastic_gw.yaml and the C++
implementation in meshtastic_ble.h / meshtastic_ble.cpp.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# ── Dependencies declared here are checked at compile time ────────────────────
# esp-idf framework is required; BLE APIs come from NimBLE via esp-idf.
DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["mqtt"]
MULTI_CONF = False  # Only one Meshtastic BLE gateway instance is supported.

# ── Namespace / class registration ────────────────────────────────────────────
meshtastic_ble_ns = cg.esphome_ns.namespace("meshtastic_ble")
MeshtasticBLEComponent = meshtastic_ble_ns.class_(
    "MeshtasticBLEComponent", cg.Component
)

# ── Config key constants ──────────────────────────────────────────────────────
CONF_NODE_NAME = "node_name"
CONF_NODE_MAC = "node_mac"
CONF_TOPIC_PREFIX = "topic_prefix"
CONF_RECONNECT_INTERVAL = "reconnect_interval"

# ── YAML schema ───────────────────────────────────────────────────────────────
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MeshtasticBLEComponent),
            # At least one of node_name or node_mac must be provided.
            cv.Optional(CONF_NODE_NAME): cv.string,
            cv.Optional(CONF_NODE_MAC): cv.mac_address,
            cv.Optional(CONF_TOPIC_PREFIX, default="meshtastic"): cv.string,
            cv.Optional(CONF_RECONNECT_INTERVAL, default=30): cv.positive_int,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


def validate(config):
    if CONF_NODE_NAME not in config and CONF_NODE_MAC not in config:
        raise cv.Invalid("At least one of 'node_name' or 'node_mac' must be set.")
    return config


CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate)


# ── Code generation ───────────────────────────────────────────────────────────
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CONF_NODE_NAME in config:
        cg.add(var.set_node_name(config[CONF_NODE_NAME]))
    if CONF_NODE_MAC in config:
        cg.add(var.set_node_mac(config[CONF_NODE_MAC].as_hex))
    cg.add(var.set_topic_prefix(config[CONF_TOPIC_PREFIX]))
    cg.add(var.set_reconnect_interval(config[CONF_RECONNECT_INTERVAL]))
