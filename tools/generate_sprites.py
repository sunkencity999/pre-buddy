#!/usr/bin/env python3
"""Render the 24-state face sprite atlas.

Output: 24 PNGs in ``firmware/esp32/main/sprites/`` matching the viewer's
SVG geometry (3 characters × 8 expressions). Run this whenever the
viewer's face design changes:

    python3 tools/generate_sprites.py

The companion ``tools/sprites_to_header.py`` reads those PNGs and emits
a C++ header of RGB565 data that the ESP32 build links in. The viewer's
SVG and these PNGs share the same geometry tables so user-side faces
match what the device renders.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError as exc:  # pragma: no cover - dev-only script
    raise SystemExit(
        "generate_sprites.py needs Pillow installed: pip install Pillow"
    ) from exc


# Sprite canvas — matches firmware/core/include/pre_buddy/sprite_atlas.h.
WIDTH = 96
HEIGHT = 80

# Background = IPS panel "off" navy; foreground = near-black face elements.
BG = (15, 20, 30, 255)
FG = (11, 15, 23, 255)


# ── geometry, mirrors viewer/viewer.js ─────────────────────────────────


@dataclass(frozen=True)
class CharGeom:
    eye_rx: float
    eye_ry: float
    eye_cy: float  # in 100×80 SVG coords; we scale to WIDTH×HEIGHT below
    eye_dx: float  # x offset from center
    brow_y: float


CHARACTERS = {
    "sage":     CharGeom(eye_rx=5,  eye_ry=5,  eye_cy=36, eye_dx=18, brow_y=22),
    "sprout":   CharGeom(eye_rx=7,  eye_ry=6,  eye_cy=34, eye_dx=20, brow_y=20),
    "sentinel": CharGeom(eye_rx=2,  eye_ry=8,  eye_cy=36, eye_dx=18, brow_y=22),
}


@dataclass(frozen=True)
class ExprOverlay:
    eye_dy: float
    eye_scale: float
    brow_dy: float
    brow_angle_deg: float  # +outer-up = worried, -outer-up = angry
    mouth_kind: str         # "flat" | "smile" | "frown" | "o" | "soft" | "ohmm" | "small_smile"
    x_eyes: bool = False


EXPRESSIONS = {
    "neutral":   ExprOverlay(eye_dy=0,  eye_scale=1.0,  brow_dy=0,  brow_angle_deg=0,   mouth_kind="flat"),
    "surprised": ExprOverlay(eye_dy=-1, eye_scale=1.2,  brow_dy=-4, brow_angle_deg=0,   mouth_kind="o"),
    "thinking":  ExprOverlay(eye_dy=-2, eye_scale=0.9,  brow_dy=-2, brow_angle_deg=18,  mouth_kind="flat"),
    "concerned": ExprOverlay(eye_dy=1,  eye_scale=0.85, brow_dy=4,  brow_angle_deg=-22, mouth_kind="frown"),
    "happy":     ExprOverlay(eye_dy=0,  eye_scale=0.6,  brow_dy=-2, brow_angle_deg=6,   mouth_kind="smile"),
    "sleepy":    ExprOverlay(eye_dy=1,  eye_scale=0.35, brow_dy=2,  brow_angle_deg=0,   mouth_kind="soft"),
    "curious":   ExprOverlay(eye_dy=-1, eye_scale=1.1,  brow_dy=-3, brow_angle_deg=10,  mouth_kind="ohmm"),
    "error":     ExprOverlay(eye_dy=0,  eye_scale=1.0,  brow_dy=-6, brow_angle_deg=-30, mouth_kind="flat", x_eyes=True),
}


# ── rendering ─────────────────────────────────────────────────────────


def _scale_x(sx: float) -> float:
    return sx * (WIDTH / 100.0)


def _scale_y(sy: float) -> float:
    return sy * (HEIGHT / 80.0)


def _draw_brow(d: ImageDraw.ImageDraw, cx: float, y: float, angle_deg: float, side: int) -> None:
    length = _scale_y(9)
    rad = math.radians(angle_deg * side)
    dx = math.cos(rad) * (length / 2)
    dy = math.sin(rad) * (length / 2)
    d.line(
        [(cx - dx, y - dy), (cx + dx, y + dy)],
        fill=FG, width=3,
    )


def _draw_mouth(d: ImageDraw.ImageDraw, kind: str) -> None:
    cx = _scale_x(50)
    # Coordinates from the viewer (svg y=60 is mouth baseline).
    if kind == "flat":
        d.line([(_scale_x(40), _scale_y(60)), (_scale_x(60), _scale_y(60))], fill=FG, width=3)
    elif kind == "small_smile":
        # Gentle upward curve.
        _quad(d, (_scale_x(42), _scale_y(60)), (cx, _scale_y(62)), (_scale_x(58), _scale_y(60)))
    elif kind == "ohmm":
        _quad(d, (_scale_x(42), _scale_y(60)), (cx, _scale_y(62)), (_scale_x(58), _scale_y(60)))
    elif kind == "smile":
        _quad(d, (_scale_x(38), _scale_y(58)), (cx, _scale_y(70)), (_scale_x(62), _scale_y(58)))
    elif kind == "frown":
        _quad(d, (_scale_x(38), _scale_y(64)), (cx, _scale_y(56)), (_scale_x(62), _scale_y(64)))
    elif kind == "o":
        # Round mouth, filled to read as an 'O' shape.
        r = _scale_y(4)
        d.ellipse((cx - r, _scale_y(60) - r, cx + r, _scale_y(60) + r), outline=FG, width=3)
    elif kind == "soft":
        _quad(d, (_scale_x(42), _scale_y(62)), (cx, _scale_y(64)), (_scale_x(58), _scale_y(62)))
    else:
        raise ValueError(f"unknown mouth kind: {kind!r}")


def _quad(d: ImageDraw.ImageDraw, p0, p1, p2, *, segments: int = 24) -> None:
    """Plot a quadratic Bézier as a polyline (Pillow has no native quad)."""
    pts = []
    for i in range(segments + 1):
        t = i / segments
        x = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t ** 2 * p2[0]
        y = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t ** 2 * p2[1]
        pts.append((x, y))
    d.line(pts, fill=FG, width=3, joint="curve")


def render_face(character: str, expression: str) -> Image.Image:
    geom = CHARACTERS[character]
    overlay = EXPRESSIONS[expression]

    img = Image.new("RGBA", (WIDTH, HEIGHT), BG)
    d = ImageDraw.Draw(img)

    eye_cy = _scale_y(geom.eye_cy + overlay.eye_dy)
    eye_rx = _scale_x(geom.eye_rx * overlay.eye_scale)
    eye_ry = _scale_y(geom.eye_ry * overlay.eye_scale)
    cx_l = _scale_x(50 - geom.eye_dx)
    cx_r = _scale_x(50 + geom.eye_dx)

    if overlay.x_eyes:
        s = _scale_y(5)
        for cx in (cx_l, cx_r):
            d.line([(cx - s, eye_cy - s), (cx + s, eye_cy + s)], fill=FG, width=3)
            d.line([(cx + s, eye_cy - s), (cx - s, eye_cy + s)], fill=FG, width=3)
    else:
        for cx in (cx_l, cx_r):
            d.ellipse(
                (cx - eye_rx, eye_cy - eye_ry, cx + eye_rx, eye_cy + eye_ry),
                fill=FG,
            )

    brow_y = _scale_y(geom.brow_y + overlay.brow_dy)
    _draw_brow(d, cx_l, brow_y, overlay.brow_angle_deg, side=+1)
    _draw_brow(d, cx_r, brow_y, overlay.brow_angle_deg, side=-1)

    _draw_mouth(d, overlay.mouth_kind)

    return img.convert("RGB")  # drop alpha — IPS panel doesn't render it anyway


# ── entrypoint ────────────────────────────────────────────────────────


def main() -> int:
    out_dir = (Path(__file__).resolve().parent.parent
               / "firmware" / "esp32" / "main" / "sprites")
    out_dir.mkdir(parents=True, exist_ok=True)

    count = 0
    for ch in CHARACTERS:
        for ex in EXPRESSIONS:
            img = render_face(ch, ex)
            path = out_dir / f"{ch}_{ex}.png"
            img.save(path, "PNG", optimize=True)
            count += 1

    print(f"wrote {count} sprites to {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
