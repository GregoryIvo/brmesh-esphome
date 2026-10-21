import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.components.light.effects import register_rgb_effect
from esphome.components.light.types import LightEffect
from esphome.const import CONF_NAME, CONF_OUTPUT_ID, CONF_ID
from esphome.core import ID

from . import brmesh_ns, BrmeshHub

CONF_ADDRESS = "address"
CONF_ADDRESSES = "addresses"
CONF_BRMESH_ID = "brmesh_id"
CONF_SUPPORTS_COLD_WARM = "supports_cold_warm"
CONF_DEFAULT_TRANSITION_LENGTH = "default_transition_length"
CONF_BUILTIN_EFFECTS = "builtin_effects"
CONF_PRESET = "preset"
CONF_COLORS = "colors"
CONF_STYLE = "style"
CONF_SPEED = "speed"
CONF_JIFFIES = "jiffies"

BrmeshLight = brmesh_ns.class_("BrmeshLight", light.LightOutput, cg.Component)
BrmeshModeEffect = brmesh_ns.class_("BrmeshModeEffect", LightEffect)

# Scene colour bytes (BLESceneInfo.parseScene): cold 16, warm 8, green 4, red 2, blue 1.
COLOR_BITS = {
    "off": 0x00,
    "blue": 0x01,
    "red": 0x02,
    "purple": 0x03,
    "green": 0x04,
    "cyan": 0x05,
    "yellow": 0x06,
    "white": 0x07,
    "warm_white": 0x08,
    "cold_white": 0x10,
}
FLASH_FLAG = 0x80
GRAYSCALE_FLAG = 0x40  # always set by the app's Mode screen

# Names and sequences from the BRmesh app (GlobalBluetooth.*Color, strings Mod1..Mod21). style: fade|flash.
PRESETS = {
    "full_color_fade": ("Full color fade", "fade", ["red", "purple", "blue", "cyan", "green", "yellow"]),
    "romantic": ("Romantic", "fade", ["purple", "red"]),
    "fresh": ("Fresh", "fade", ["cyan", "blue", "green"]),
    "cozy": ("Cozy", "fade", ["yellow", "red"]),
    "full_color_flash": ("Full color flash", "flash", ["red", "purple", "blue", "cyan", "green", "yellow"]),
    "three_color_flash": ("Three color flash", "flash", ["red", "green", "blue"]),
    "yellow_green_flash": ("Yellow and green flash", "flash", ["yellow", "green"]),
    "purple_green_flash": ("Purple and green flash", "flash", ["purple", "green"]),
    "purple_blue_flash": ("Purple and blue flash", "flash", ["purple", "blue"]),
    "yellow_purple_flash": ("Yellow and purple flash", "flash", ["yellow", "purple"]),
    "red_green_flash": ("Red and green flash", "flash", ["red", "green"]),
    "red_blue_flash": ("Red and blue flash", "flash", ["red", "blue"]),
    "green_blue_flash": ("Green and blue flash", "flash", ["green", "blue"]),
    "red_flash": ("Red flash", "flash", ["red", "off"]),
    "green_flash": ("Green flash", "flash", ["green", "off"]),
    "blue_flash": ("Blue flash", "flash", ["blue", "off"]),
    "white_flash": ("White flash", "flash", ["white", "off"]),
    "christmas": ("Christmas", "fade", ["red", "green"]),
    "winter": ("Winter", "fade", ["blue", "white"]),
    "halloween": ("Halloween", "fade", ["green", "white"]),
    "valentines_day": ("Valentines day", "fade", ["red", "white"]),
}


def scene_bytes(style, colors):
    out = [COLOR_BITS[c] for c in colors]
    out[0] |= GRAYSCALE_FLAG | (FLASH_FLAG if style == "flash" else 0)
    return out


def speed_to_jiffies(style, speed):
    # ModFragment.speedExchange: fade modes count up from 1, flash modes count down from 200 in steps of 2.
    if style == "flash":
        return max(1, 200 - (speed - 1) * 2)
    return speed


def validate_mode_effect(config):
    if (CONF_PRESET in config) == (CONF_COLORS in config):
        raise cv.Invalid("Give either preset or colors for a brmesh_mode effect")
    if CONF_COLORS in config and CONF_STYLE not in config:
        config = dict(config)
        config[CONF_STYLE] = "fade"
    return config


@register_rgb_effect(
    "brmesh_mode",
    BrmeshModeEffect,
    "BRMesh Mode",
    {
        cv.Optional(CONF_PRESET): cv.one_of(*PRESETS, lower=True),
        cv.Optional(CONF_COLORS): cv.All(
            cv.ensure_list(cv.one_of(*COLOR_BITS, lower=True)), cv.Length(min=1, max=9)
        ),
        cv.Optional(CONF_STYLE): cv.one_of("fade", "flash", lower=True),
        cv.Optional(CONF_SPEED, default=1): cv.int_range(min=1, max=100),
        cv.Optional(CONF_JIFFIES): cv.int_range(min=0, max=255),
    },
    validate_mode_effect,
)
async def brmesh_mode_effect_to_code(config, effect_id):
    var = cg.new_Pvariable(effect_id, config[CONF_NAME])
    if CONF_PRESET in config:
        _, style, colors = PRESETS[config[CONF_PRESET]]
        style = config.get(CONF_STYLE, style)
    else:
        style = config[CONF_STYLE]
        colors = config[CONF_COLORS]
    cg.add(var.set_scene(scene_bytes(style, colors)))
    cg.add(var.set_jiffies(config.get(CONF_JIFFIES, speed_to_jiffies(style, config[CONF_SPEED]))))
    return var


def validate_address_mode(config):
    if CONF_ADDRESS not in config and CONF_ADDRESSES not in config:
        raise cv.Invalid("Either address or addresses is required")
    if CONF_ADDRESS in config and CONF_ADDRESSES in config:
        raise cv.Invalid("Use either address for one light or addresses for a grouped light, not both")
    return config


CONFIG_SCHEMA = cv.All(
    light.RGB_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(BrmeshLight),
            cv.GenerateID(CONF_BRMESH_ID): cv.use_id(BrmeshHub),
            cv.Optional(CONF_ADDRESS): cv.int_range(min=1, max=65535),
            cv.Optional(CONF_ADDRESSES): cv.ensure_list(cv.int_range(min=1, max=65535)),
            cv.Optional(CONF_SUPPORTS_COLD_WARM, default=True): cv.boolean,
            cv.Optional(CONF_DEFAULT_TRANSITION_LENGTH, default="0s"): cv.positive_time_period_milliseconds,
            # Add the app's 21 built-in fade/flash modes as effects. Set false to keep the list short.
            cv.Optional(CONF_BUILTIN_EFFECTS, default=True): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_address_mode,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await light.register_light(var, config)
    parent = await cg.get_variable(config[CONF_BRMESH_ID])
    cg.add(var.set_parent(parent))
    if CONF_ADDRESS in config:
        cg.add(var.set_address(config[CONF_ADDRESS]))
    if CONF_ADDRESSES in config:
        cg.add(var.set_addresses(config[CONF_ADDRESSES]))
    cg.add(var.set_supports_cold_warm(config[CONF_SUPPORTS_COLD_WARM]))
    if config[CONF_BUILTIN_EFFECTS]:
        state = await cg.get_variable(config[CONF_ID])
        effects = []
        for key, (name, style, colors) in PRESETS.items():
            effect_id = ID(f"{config[CONF_OUTPUT_ID].id}_mode_{key}", is_declaration=True, type=BrmeshModeEffect)
            effect = cg.new_Pvariable(effect_id, name)
            cg.add(effect.set_scene(scene_bytes(style, colors)))
            cg.add(effect.set_jiffies(speed_to_jiffies(style, 1)))
            effects.append(effect)
        cg.add(state.add_effects(effects))
