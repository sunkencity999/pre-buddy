"""Tests for buddy_control → device-line translation."""

from __future__ import annotations

import json

from pre_buddy.buddy import buddy_config_payload, device_lines


def _by_event(lines):
    return {json.loads(l)["event"]: json.loads(l) for l in lines}


def test_config_fields_become_one_config_line():
    lines = device_lines({"led_color": "cyan", "volume": 80, "idle_anim": False})
    ev = _by_event(lines)
    assert set(ev) == {"pre.buddy.config"}
    assert ev["pre.buddy.config"]["data"] == {
        "led_color": "cyan", "volume": 80, "idle_anim": False}


def test_character_becomes_separate_event():
    lines = device_lines({"character": "sprout"})
    ev = _by_event(lines)
    assert set(ev) == {"pre.character.set"}
    assert ev["pre.character.set"]["data"]["character"] == "sprout"


def test_config_and_character_together_emit_two_lines():
    lines = device_lines({"led_color": "red", "character": "sentinel"})
    assert set(_by_event(lines)) == {"pre.buddy.config", "pre.character.set"}


def test_invalid_color_and_character_are_dropped():
    assert device_lines({"led_color": "chartreuse"}) == []
    assert device_lines({"character": "gandalf"}) == []


def test_out_of_range_values_are_clamped():
    p = buddy_config_payload({"volume": 250, "led_brightness": -5})
    assert p == {"volume": 100, "led_brightness": 0}


def test_auto_color_clears_override():
    p = buddy_config_payload({"led_color": "auto"})
    assert p == {"led_color": "auto"}


def test_empty_settings_yield_no_lines():
    assert device_lines({}) == []
    assert buddy_config_payload({}) is None


def test_non_bool_anim_flags_ignored():
    # Only real booleans count — a stray string shouldn't reach the device.
    assert buddy_config_payload({"idle_anim": "yes"}) is None
