import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import esp32_ble
from esphome.components.esp32 import add_idf_sdkconfig_option
from esphome.const import CONF_ID, CONF_TRIGGER_ID

CODEOWNERS = ["@local"]
DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["esp32_ble"]

brmesh_ns = cg.esphome_ns.namespace("brmesh")
BrmeshHub = brmesh_ns.class_("BrmeshHub", cg.Component)
BrmeshSendRawAction = brmesh_ns.class_("BrmeshSendRawAction", automation.Action)
BrmeshSendSceneAction = brmesh_ns.class_("BrmeshSendSceneAction", automation.Action)
BrmeshStartPairingAction = brmesh_ns.class_("BrmeshStartPairingAction", automation.Action)
BrmeshStopPairingAction = brmesh_ns.class_("BrmeshStopPairingAction", automation.Action)
BrmeshClearPairingsAction = brmesh_ns.class_("BrmeshClearPairingsAction", automation.Action)
BrmeshEnrollDeviceAction = brmesh_ns.class_("BrmeshEnrollDeviceAction", automation.Action)
BrmeshGroupOnOffAction = brmesh_ns.class_("BrmeshGroupOnOffAction", automation.Action)
BrmeshTempGroupOnOffAction = brmesh_ns.class_("BrmeshTempGroupOnOffAction", automation.Action)
BrmeshDevicePairedTrigger = brmesh_ns.class_(
    "BrmeshDevicePairedTrigger", automation.Trigger.template(cg.uint16, cg.std_string)
)

CONF_KEY = "key"
CONF_REDUNDANCY = "redundancy"
CONF_COMMAND_INTERVAL = "command_interval"
CONF_QUERY_AFTER_WRITE = "query_after_write"
CONF_QUERY_REDUNDANCY = "query_redundancy"
CONF_STATE_QUERY_ON_BOOT = "state_query_on_boot"
CONF_STATE_POLL_INTERVAL = "state_poll_interval"
CONF_STATE_TIMEOUT = "state_timeout"
CONF_ALL_GROUP_REFRESH_ON_BOOT = "all_group_refresh_on_boot"
CONF_COMMAND_AIRTIME = "command_airtime"
CONF_GROUP_REFRESH_INTERVAL = "group_refresh_interval"
CONF_LISTEN = "listen"
CONF_RECOVER_BLE = "recover_ble"
CONF_RETRY_DROPPED_WRITES = "retry_dropped_writes"
CONF_PAIRING_ADDRESSES = "pairing_addresses"
CONF_PAIRING_TIMEOUT = "pairing_timeout"
CONF_ON_DEVICE_PAIRED = "on_device_paired"
CONF_SEQUENCE = "sequence"
CONF_PAYLOAD = "payload"
CONF_SCENE = "scene"
CONF_ADDRESS = "address"
CONF_DID = "did"
CONF_DEVICE_KEY = "device_key"
CONF_TYPE = "type"
CONF_GROUP_ID = "group_id"
CONF_STATE = "state"
CONF_ADDRESSES = "addresses"


def validate_hex_key(value):
    value = cv.string_strict(value).replace(" ", "").replace(":", "").replace("-", "")
    if len(value) != 8:
        raise cv.Invalid("BRmesh key must be exactly 4 bytes / 8 hex characters")
    try:
        int(value, 16)
    except ValueError as err:
        raise cv.Invalid("BRmesh key must be hex") from err
    return value.lower()


def validate_hex_payload(value):
    value = cv.string_strict(value).replace(" ", "").replace(":", "").replace("-", "")
    if len(value) % 2 != 0:
        raise cv.Invalid("payload must contain an even number of hex characters")
    if len(value) > 36:
        raise cv.Invalid("BRmesh inner payloads are limited to 18 bytes")
    try:
        int(value or "0", 16)
    except ValueError as err:
        raise cv.Invalid("payload must be hex") from err
    return value.lower()


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(BrmeshHub),
        cv.GenerateID(esp32_ble.CONF_BLE_ID): cv.use_id(esp32_ble.ESP32BLE),
        cv.Required(CONF_KEY): validate_hex_key,
        # How long each control packet stays on air. The BRmesh app uses 3 s; 1.5 s is a good balance
        # between reliability and responsiveness. Overrides redundancy * command_interval when set.
        cv.Optional(CONF_COMMAND_AIRTIME): cv.positive_time_period_milliseconds,
        # Legacy knobs: airtime = redundancy * command_interval (defaults give 1.5 s).
        cv.Optional(CONF_REDUNDANCY, default=5): cv.int_range(min=1, max=20),
        cv.Optional(CONF_COMMAND_INTERVAL, default="300ms"): cv.positive_time_period_milliseconds,
        # 0s = re-send temporary group membership before every group command (safest, like the app's All
        # On/Off buttons). Larger values reuse fresh membership for snappier repeated commands.
        cv.Optional(CONF_GROUP_REFRESH_INTERVAL, default="0s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_QUERY_AFTER_WRITE, default=False): cv.boolean,
        cv.Optional(CONF_QUERY_REDUNDANCY, default=1): cv.int_range(min=1, max=20),
        cv.Optional(CONF_STATE_QUERY_ON_BOOT, default=False): cv.boolean,
        cv.Optional(CONF_STATE_POLL_INTERVAL, default="0s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_STATE_TIMEOUT, default="10s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ALL_GROUP_REFRESH_ON_BOOT, default=False): cv.boolean,
        # Keep the BLE scanner running to log/decode bulb frames even when no query feature is on.
        cv.Optional(CONF_LISTEN, default=False): cv.boolean,
        # Restart the BLE stack after two consecutive dropped bursts instead of waiting for a manual reset.
        cv.Optional(CONF_RECOVER_BLE, default=True): cv.boolean,
        # Re-send a light's last requested state once after a dropped burst / BLE restart.
        cv.Optional(CONF_RETRY_DROPPED_WRITES, default=True): cv.boolean,
        # Addresses handed out to new bulbs while pairing. Default: every address a light entity declares.
        cv.Optional(CONF_PAIRING_ADDRESSES): cv.ensure_list(cv.int_range(min=1, max=65535)),
        # Pairing mode switches itself off after this long.
        cv.Optional(CONF_PAIRING_TIMEOUT, default="5min"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ON_DEVICE_PAIRED): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(BrmeshDevicePairedTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_key_hex(config[CONF_KEY]))
    cg.add(var.set_redundancy(config[CONF_REDUNDANCY]))
    cg.add(var.set_command_interval(config[CONF_COMMAND_INTERVAL]))
    cg.add(var.set_query_after_write(config[CONF_QUERY_AFTER_WRITE]))
    cg.add(var.set_query_redundancy(config[CONF_QUERY_REDUNDANCY]))
    cg.add(var.set_state_query_on_boot(config[CONF_STATE_QUERY_ON_BOOT]))
    cg.add(var.set_state_poll_interval(config[CONF_STATE_POLL_INTERVAL]))
    cg.add(var.set_state_timeout(config[CONF_STATE_TIMEOUT]))
    cg.add(var.set_all_group_refresh_on_boot(config[CONF_ALL_GROUP_REFRESH_ON_BOOT]))
    if CONF_COMMAND_AIRTIME in config:
        cg.add(var.set_command_airtime(config[CONF_COMMAND_AIRTIME]))
    cg.add(var.set_group_refresh_interval(config[CONF_GROUP_REFRESH_INTERVAL]))
    cg.add(var.set_listen(config[CONF_LISTEN]))
    cg.add(var.set_recover_ble(config[CONF_RECOVER_BLE]))
    cg.add(var.set_retry_dropped_writes(config[CONF_RETRY_DROPPED_WRITES]))
    if CONF_PAIRING_ADDRESSES in config:
        cg.add(var.set_pairing_addresses(config[CONF_PAIRING_ADDRESSES]))
    cg.add(var.set_pairing_timeout(config[CONF_PAIRING_TIMEOUT]))
    for conf in config.get(CONF_ON_DEVICE_PAIRED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint16, "address"), (cg.std_string, "did")], conf)
    ble = await cg.get_variable(config[esp32_ble.CONF_BLE_ID])
    esp32_ble.register_gap_event_handler(ble, var)
    esp32_ble.register_gap_scan_event_handler(ble, var)
    cg.add_define("USE_ESP32_BLE_ADVERTISING")
    cg.add_define("USE_ESP32_BLE_UUID")
    add_idf_sdkconfig_option("CONFIG_BT_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_BLE_42_FEATURES_SUPPORTED", True)


BRMESH_SEND_RAW_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(BrmeshHub),
        cv.Required(CONF_PAYLOAD): cv.templatable(validate_hex_payload),
        cv.Optional(CONF_SEQUENCE, default=0): cv.templatable(cv.int_range(min=0, max=255)),
    }
)


@automation.register_action(
    "brmesh.send_raw", BrmeshSendRawAction, BRMESH_SEND_RAW_SCHEMA, synchronous=True
)
async def brmesh_send_raw_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    payload = await cg.templatable(config[CONF_PAYLOAD], args, cg.std_string)
    cg.add(var.set_payload_hex(payload))
    sequence = await cg.templatable(config[CONF_SEQUENCE], args, cg.uint8)
    cg.add(var.set_sequence(sequence))
    return var


BRMESH_SEND_SCENE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(BrmeshHub),
        cv.Required(CONF_SCENE): cv.templatable(cv.int_range(min=1, max=255)),
    }
)


@automation.register_action(
    "brmesh.send_scene", BrmeshSendSceneAction, BRMESH_SEND_SCENE_SCHEMA, synchronous=True
)
async def brmesh_send_scene_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    scene = await cg.templatable(config[CONF_SCENE], args, cg.uint8)
    cg.add(var.set_scene(scene))
    return var


BRMESH_HUB_ACTION_SCHEMA = cv.Schema({cv.GenerateID(): cv.use_id(BrmeshHub)})


@automation.register_action(
    "brmesh.start_pairing", BrmeshStartPairingAction, BRMESH_HUB_ACTION_SCHEMA, synchronous=True
)
async def brmesh_start_pairing_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


@automation.register_action(
    "brmesh.stop_pairing", BrmeshStopPairingAction, BRMESH_HUB_ACTION_SCHEMA, synchronous=True
)
async def brmesh_stop_pairing_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


@automation.register_action(
    "brmesh.clear_pairings", BrmeshClearPairingsAction, BRMESH_HUB_ACTION_SCHEMA, synchronous=True
)
async def brmesh_clear_pairings_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


def validate_hex_did(value):
    value = cv.string_strict(value).replace(" ", "").replace(":", "").replace("-", "")
    if len(value) != 12:
        raise cv.Invalid("BRmesh DID must be exactly 6 bytes / 12 hex characters")
    try:
        int(value, 16)
    except ValueError as err:
        raise cv.Invalid("BRmesh DID must be hex") from err
    return value.lower()


BRMESH_ENROLL_DEVICE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(BrmeshHub),
        cv.Required(CONF_ADDRESS): cv.templatable(cv.int_range(min=1, max=65535)),
        cv.Required(CONF_DID): cv.templatable(validate_hex_did),
        cv.Required(CONF_DEVICE_KEY): cv.templatable(validate_hex_key),
        cv.Optional(CONF_TYPE, default=0): cv.templatable(cv.int_range(min=0, max=65535)),
        cv.Optional(CONF_GROUP_ID, default=0): cv.templatable(cv.int_range(min=0, max=65535)),
    }
)


@automation.register_action(
    "brmesh.enroll_device", BrmeshEnrollDeviceAction, BRMESH_ENROLL_DEVICE_SCHEMA, synchronous=True
)
async def brmesh_enroll_device_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    address = await cg.templatable(config[CONF_ADDRESS], args, cg.uint16)
    did = await cg.templatable(config[CONF_DID], args, cg.std_string)
    device_key = await cg.templatable(config[CONF_DEVICE_KEY], args, cg.std_string)
    type_ = await cg.templatable(config[CONF_TYPE], args, cg.uint16)
    group_id = await cg.templatable(config[CONF_GROUP_ID], args, cg.uint16)
    cg.add(var.set_address(address))
    cg.add(var.set_did(did))
    cg.add(var.set_device_key(device_key))
    cg.add(var.set_type(type_))
    cg.add(var.set_group_id(group_id))
    return var


BRMESH_GROUP_ON_OFF_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(BrmeshHub),
        cv.Optional(CONF_TYPE, default=43050): cv.templatable(cv.int_range(min=0, max=65535)),
        cv.Required(CONF_GROUP_ID): cv.templatable(cv.int_range(min=0, max=255)),
        cv.Required(CONF_STATE): cv.templatable(cv.boolean),
    }
)


@automation.register_action(
    "brmesh.group_on_off", BrmeshGroupOnOffAction, BRMESH_GROUP_ON_OFF_SCHEMA, synchronous=True
)
async def brmesh_group_on_off_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    type_ = await cg.templatable(config[CONF_TYPE], args, cg.uint16)
    group_id = await cg.templatable(config[CONF_GROUP_ID], args, cg.uint8)
    state = await cg.templatable(config[CONF_STATE], args, cg.bool_)
    cg.add(var.set_type(type_))
    cg.add(var.set_group_id(group_id))
    cg.add(var.set_state(state))
    return var


BRMESH_TEMP_GROUP_ON_OFF_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(BrmeshHub),
        cv.Required(CONF_ADDRESSES): cv.ensure_list(cv.int_range(min=1, max=65535)),
        cv.Required(CONF_STATE): cv.templatable(cv.boolean),
    }
)


@automation.register_action(
    "brmesh.temp_group_on_off", BrmeshTempGroupOnOffAction, BRMESH_TEMP_GROUP_ON_OFF_SCHEMA, synchronous=True
)
async def brmesh_temp_group_on_off_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    cg.add(var.set_addresses(config[CONF_ADDRESSES]))
    state = await cg.templatable(config[CONF_STATE], args, cg.bool_)
    cg.add(var.set_state(state))
    return var
