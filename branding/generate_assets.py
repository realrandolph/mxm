#!/usr/bin/env python3
"""Regenerate all MXM branding derivatives from the canonical source graphics.

Sources (source-of-truth, never modified here):
    branding/source/MXM-square.png   1254x1254 RGBA (application icon art)
    branding/source/MXM-banner.png   1733x907  RGB  (wide project branding)

This script is deterministic: it only downsamples (never upscales) and uses
ImageMagick's Lanczos filter for high-quality resampling, preserving alpha
wherever the target format supports it.

Requirements: ImageMagick (`convert`), Python 3 (stdlib only).

Usage:  python3 branding/generate_assets.py
"""

import base64
import os
import shutil
import struct
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "branding", "source")
SQUARE = os.path.join(SOURCE, "MXM-square.png")
BANNER = os.path.join(SOURCE, "MXM-banner.png")

# ImageMagick 6 interprets "@" in output paths (the hicolor "16x16@2" dirs) as
# a special suffix, so always write to a temp path first and atomically move.
_TMPDIR = tempfile.mkdtemp(prefix="mxm-assets-")


def convert(*args):
    subprocess.run(["convert", *args], check=True)


def _write_derivative(tmp_path, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.move(tmp_path, dst)


def lanczos_resize(src, width, height, dst):
    tmp = os.path.join(_TMPDIR, os.path.basename(dst) + ".tmp.png")
    convert(src, "-filter", "Lanczos", "-resize", f"{width}x{height}!",
            "-strip", tmp)
    _write_derivative(tmp, dst)


def fit_resize(src, width, height, dst):
    """Scale src to fit within (width, height) preserving aspect ratio."""
    tmp = os.path.join(_TMPDIR, os.path.basename(dst) + ".tmp.png")
    convert(src, "-filter", "Lanczos", "-resize", f"{width}x{height}",
            "-strip", tmp)
    _write_derivative(tmp, dst)


def write_icns(entries, outpath):
    """Build a macOS .icns container from (type, png_data) entries."""
    body = b""
    total = 8
    for typ, data in entries:
        length = 8 + len(data)
        body += typ.encode("ascii") + struct.pack(">I", length) + data
        total += length
    with open(outpath, "wb") as f:
        f.write(b"icns" + struct.pack(">I", total) + body)


# ---------------------------------------------------------------------------
# Square-derived application/file icons (transparency preserved)
# ---------------------------------------------------------------------------

def generate_square_icons():
    tmp = _TMPDIR
    hicolor = os.path.join(ROOT, "cmake", "linux", "icons")

    # hicolor raster sizes + @2x counterparts
    at1 = {"16x16": 16, "24x24": 24, "32x32": 32, "48x48": 48,
           "64x64": 64, "128x128": 128, "256x256": 256}
    at2 = {"16x16@2": 32, "24x24@2": 48, "32x32@2": 64, "48x48@2": 96,
           "64x64@2": 128, "128x128@2": 256}
    targets = (("apps", "mxm.png"),
               ("mimetypes", "application-x-mxm-project.png"))
    for d, s in at1.items():
        for sub, name in targets:
            lanczos_resize(SQUARE, s, s, os.path.join(hicolor, d, sub, name))
    for d, s in at2.items():
        for sub, name in targets:
            lanczos_resize(SQUARE, s, s, os.path.join(hicolor, d, sub, name))

    # scalable SVG wrappers (embed a deterministic 512px render)
    png512 = os.path.join(tmp, "square-512.png")
    lanczos_resize(SQUARE, 512, 512, png512)
    with open(png512, "rb") as f:
        b64 = base64.b64encode(f.read()).decode("ascii")
    svg = ('<?xml version="1.0" encoding="UTF-8"?>\n'
           '<svg xmlns="http://www.w3.org/2000/svg" '
           'xmlns:xlink="http://www.w3.org/1999/xlink" '
           'width="512" height="512" viewBox="0 0 512 512">\n'
           '  <image width="512" height="512" '
           f'xlink:href="data:image/png;base64,{b64}"/>\n'
           '</svg>\n')
    for path in ("apps/mxm.svg", "mimetypes/application-x-mxm-project.svg"):
        with open(os.path.join(hicolor, "scalable", path), "w") as f:
            f.write(svg)

    # in-theme icons used by the Qt runtime (window + About dialog)
    for theme in ("default", "classic"):
        tdir = os.path.join(ROOT, "data", "themes", theme)
        lanczos_resize(SQUARE, 128, 128, os.path.join(tdir, "icon.png"))
        lanczos_resize(SQUARE, 32, 32, os.path.join(tdir, "icon_small.png"))

    # Windows ICO (app + project file type share the canonical mark)
    frames = []
    for s in (16, 24, 32, 48, 64, 96, 128, 256):
        p = os.path.join(tmp, f"ico-{s}.png")
        lanczos_resize(SQUARE, s, s, p)
        frames.append(p)
    convert(*(frames), os.path.join(ROOT, "cmake", "nsis", "icon.ico"))
    convert(*(frames), os.path.join(ROOT, "cmake", "nsis", "project.ico"))

    # Windows Start-tile logos
    lanczos_resize(SQUARE, 150, 150,
                   os.path.join(ROOT, "cmake", "nsis", "assets", "Logo.png"))
    lanczos_resize(SQUARE, 70, 70,
                   os.path.join(ROOT, "cmake", "nsis", "assets", "SmallLogo.png"))

    # macOS ICNS (app + project file type)
    icns_sizes = (("icp4", 16), ("icp5", 32), ("ic07", 128), ("ic08", 256),
                  ("ic09", 512), ("ic10", 1024))
    for name in ("icon.icns", "project.icns"):
        entries = []
        for typ, s in icns_sizes:
            p = os.path.join(tmp, f"icns-{s}.png")
            lanczos_resize(SQUARE, s, s, p)
            with open(p, "rb") as f:
                entries.append((typ, f.read()))
        write_icns(entries, os.path.join(ROOT, "cmake", "apple", name))

    print("square-derived assets generated")


# ---------------------------------------------------------------------------
# Banner-derived wide branding
# ---------------------------------------------------------------------------

def generate_banner_assets():
    tmp = _TMPDIR

    # Splash screens (both themes): banner scaled to 1024px wide
    splash = os.path.join(tmp, "splash.png")
    fit_resize(BANNER, 1024, 536, splash)
    for theme in ("default", "classic"):
        convert(splash, os.path.join(ROOT, "data", "themes", theme, "splash.png"))

    # README header banner (native aspect, 1200px wide)
    fit_resize(BANNER, 1200, 628,
               os.path.join(ROOT, "branding", "README-banner.png"))

    # OpenGraph 1200x630 (1.90476:1): banner is 1.9107:1 -> 1200x628 + 2px pad
    og_fit = os.path.join(tmp, "og-fit.png")
    fit_resize(BANNER, 1200, 630, og_fit)
    convert("-size", "1200x630", "xc:none", og_fit, "-gravity", "center",
            "-composite", "-strip",
            os.path.join(ROOT, "branding", "social", "opengraph-1200x630.png"))

    # GitHub social 1280x640 (2:1): banner is narrower -> transparent side pad
    gh_fit = os.path.join(tmp, "gh-fit.png")
    fit_resize(BANNER, 1280, 640, gh_fit)
    convert("-size", "1280x640", "xc:none", gh_fit, "-gravity", "center",
            "-composite", "-strip",
            os.path.join(ROOT, "branding", "social",
                         "github-social-1280x640.png"))

    # NSIS installer branding strip (150x57): BMP has no alpha, so flatten the
    # now-transparent banner onto the light-grey it was originally designed on.
    strip = os.path.join(tmp, "nsis-strip-fit.png")
    fit_resize(BANNER, 150, 57, strip)
    convert("-size", "150x57", "xc:#C2C2C2", strip, "-gravity", "center",
            "-composite", "-colors", "256",
            "BMP3:" + os.path.join(ROOT, "cmake", "nsis", "nsis_branding.bmp"))

    print("banner-derived assets generated")


def generate_dmg_background():
    """Neutral light-grey DMG backdrop (removes inherited LMMS green mark)."""
    for path, w, h in (("background.png", 705, 400),
                       ("background@2x.png", 1410, 800)):
        convert("-size", f"{w}x{h}", "gradient:#EBEBEB-#D5D5D5",
                os.path.join(ROOT, "cmake", "apple", path))
    print("DMG background generated")


def main():
    generate_square_icons()
    generate_banner_assets()
    generate_dmg_background()
    print("done")


if __name__ == "__main__":
    main()
