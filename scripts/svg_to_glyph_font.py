#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Convert a single monochrome SVG (e.g. the M17 logo) into a one-glyph TTF so
# it can be merged into the UI font by lv_font_conv, exactly like a FontAwesome
# icon. lv_font_conv only ingests font files (TTF/OTF/WOFF), not raw SVGs, so
# this bridges an SVG asset into that pipeline.
#
# The glyph is placed at a Private-Use-Area codepoint (default 0xE900) and
# scaled to sit on the baseline at roughly cap height, so it drops into a line
# of text like any other icon glyph.
#
# Usage:  scripts/svg_to_glyph_font.py IN.svg OUT.ttf [--codepoint 0xE900]
#
# Requires: fonttools (pip install fonttools).

import argparse
import xml.etree.ElementTree as ET

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.recordingPen import RecordingPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.svgLib.path import parse_path

UPEM = 1000       # units per em
CAP_H = 950       # glyph height in em units; tuned so the mark reads at a
                  # weight comparable to the mode-label text it sits beside
                  # (a thin wordmark needs more height than nominal cap height)
SIDE = 80         # left/right side bearing in em units
_SVG_NS = "{http://www.w3.org/2000/svg}"


def _iter_paths(root, exclude=()):
    """Yield the 'd' attribute of every <path> in document order, skipping any
    whose style/fill contains one of the `exclude` colour substrings (used to
    drop a shadow layer)."""
    for el in root.iter():
        if el.tag == _SVG_NS + "path" or el.tag == "path":
            d = el.get("d")
            if not d:
                continue
            attrs = (el.get("style") or "") + " " + (el.get("fill") or "")
            if any(c.lower() in attrs.lower() for c in exclude):
                continue
            yield d


def convert(svg_path, ttf_path, codepoint, exclude=()):
    tree = ET.parse(svg_path)
    root = tree.getroot()

    paths = list(_iter_paths(root, exclude))
    if not paths:
        raise SystemExit(f"{svg_path}: no <path> elements found")

    # Fit the actual drawn geometry (not the viewBox, which may carry large
    # empty margins) to CAP_H, so the mark renders at cap height like adjacent
    # text. NOTE: ancestor <g transform> values are assumed to be translations
    # only (the usual Inkscape export) — they shift position, which we discard
    # by re-anchoring to the baseline, so they do not affect the fitted size.
    # A group scale/rotation would need flattening in the source SVG first.
    bp = BoundsPen(glyphSet=None)
    for d in paths:
        parse_path(d, bp)
    if bp.bounds is None:
        raise SystemExit(f"{svg_path}: paths enclose no area (all zero-width "
                         f"strokes? run stroke-to-path first)")
    minx, miny, maxx, maxy = bp.bounds
    cw, ch = maxx - minx, maxy - miny
    if cw <= 0 or ch <= 0:
        raise SystemExit(f"{svg_path}: degenerate content bounds {bp.bounds}")

    # SVG is y-down from the top-left; fonts are y-up from the baseline. Scale
    # content height to CAP_H, flip Y so the content bottom lands on the
    # baseline (y=0) and its top at CAP_H, and inset by SIDE on the left.
    s = CAP_H / ch
    xform = (s, 0, 0, -s, -minx * s + SIDE, maxy * s)

    # Replay each path outline through the flip/scale transform into one TT
    # glyph. parse_path() draws a single "d" string; TransformPen applies the
    # SVG-to-font coordinate mapping on the way.
    pen = TTGlyphPen(glyphSet=None)
    rec = RecordingPen()
    tpen = TransformPen(rec, xform)
    for d in paths:
        parse_path(d, tpen)
    rec.replay(pen)
    glyph = pen.glyph()

    advance = round(cw * s + 2 * SIDE)
    glyph_name = "m17logo"
    order = [".notdef", glyph_name]
    fb = FontBuilder(UPEM, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap({codepoint: glyph_name})

    notdef = TTGlyphPen(None).glyph()
    fb.setupGlyf({".notdef": notdef, glyph_name: glyph})
    fb.setupHorizontalMetrics({".notdef": (advance, 0), glyph_name: (advance, 0)})
    fb.setupHorizontalHeader(ascent=800, descent=-200)
    fb.setupNameTable({"familyName": "M17Logo", "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800,
                usWinDescent=200)
    fb.setupPost()
    fb.save(ttf_path)
    print(f"wrote {ttf_path}: U+{codepoint:04X} '{glyph_name}', "
          f"advance {advance}/{UPEM} em")


def main():
    ap = argparse.ArgumentParser(description="SVG -> one-glyph TTF for lv_font_conv")
    ap.add_argument("svg")
    ap.add_argument("ttf")
    ap.add_argument("--codepoint", default="0xE900",
                    help="Unicode code point for the glyph (default 0xE900)")
    ap.add_argument("--exclude", action="append", default=[], metavar="COLOR",
                    help="Skip paths whose style/fill contains COLOR (e.g. a "
                         "shadow layer #999999); repeatable")
    args = ap.parse_args()
    convert(args.svg, args.ttf, int(args.codepoint, 0), exclude=args.exclude)


if __name__ == "__main__":
    main()
