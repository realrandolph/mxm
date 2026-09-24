# MXM branding

This directory holds the canonical MXM branding artwork and the tooling that
derives every platform/package asset from it.

## Canonical sources (source of truth)

| File | Dimensions | Format | Purpose |
|------|-----------|--------|---------|
| `source/MXM-square.png` | 1254×1254 | PNG (RGBA) | Application icon / square branding |
| `source/MXM-banner.png` | 1733×907  | PNG (RGB)  | Wide project branding |

These two files are the masters. They are never edited, recolored, cropped, or
recreated here — all derivatives are generated from them by
[`generate_assets.py`](generate_assets.py) using ImageMagick's Lanczos filter
(high-quality downsampling only; derivatives are never upscaled past the source
resolution, and transparency is preserved wherever the target format supports
it).

## Regenerating assets

```sh
python3 branding/generate_assets.py
```

Requirements: ImageMagick (`convert`) and Python 3 (standard library only).
The script is deterministic, so re-running it reproduces byte-identical pixel
output for the raster derivatives.

## Generated derivatives

### Application icon (from `MXM-square.png`)

| Destination | Sizes / format | Consumed by |
|-------------|----------------|-------------|
| `cmake/linux/icons/{16,24,32,48,64,128,256}x{…}/apps/mxm.png` + `@2` | PNG | Linux hicolor icon theme (`.desktop` `Icon=mxm`) |
| `cmake/linux/icons/scalable/apps/mxm.svg` | SVG (embeds 512 px) | Linux scalable fallback, Doxygen logo |
| `data/themes/{default,classic}/icon.png` | 128×128 | Qt About dialog (`embed::getIconPixmap("icon", 64, 64)`) |
| `data/themes/{default,classic}/icon_small.png` | 32×32 | Qt window/taskbar icon (`setWindowIcon(... "icon_small")`) |
| `cmake/nsis/icon.ico` | 16/24/32/48/64/96/128/256 | Windows executable resource + installer icon |
| `cmake/nsis/assets/Logo.png` | 150×150 | Windows Start-tile `Square150x150Logo` |
| `cmake/nsis/assets/SmallLogo.png` | 70×70 | Windows Start-tile `Square70x70Logo`/`Square44x44Logo` |
| `cmake/apple/icon.icns` | 16/32/128/256/512/1024 | macOS `CFBundleIconFile` (app icon) |

### Project file icon (from `MXM-square.png`)

The `.mmp`/`.mmpz` file type reuses the canonical square mark (no invented
document/note artwork).

| Destination | Sizes / format | Consumed by |
|-------------|----------------|-------------|
| `cmake/linux/icons/*/mimetypes/application-x-mxm-project.png` + `@2` + `scalable/*.svg` | PNG/SVG | Linux MIME type icon |
| `cmake/nsis/project.ico` | 16/24/32/48/64/96/128/256 | Windows file-type icon |
| `cmake/apple/project.icns` | 16/32/128/256/512/1024 | macOS document icon |

### Wide branding (from `MXM-banner.png`)

| Destination | Dimensions | Purpose |
|-------------|-----------|---------|
| `branding/README-banner.png` | 1200×628 | Repository README header |
| `branding/social/opengraph-1200x630.png` | 1200×630 | OpenGraph social preview (upload manually) |
| `branding/social/github-social-1280x640.png` | 1280×640 | GitHub repository social preview (upload manually) |
| `data/themes/{default,classic}/splash.png` | 1024×536 | Qt splash screen (`embed::getIconPixmap("splash")`) |
| `cmake/nsis/nsis_branding.bmp` | 150×57 | NSIS installer branding (`CPACK_PACKAGE_ICON`) |

### Package artwork

| Destination | Dimensions | Purpose |
|-------------|-----------|---------|
| `cmake/apple/background.png` / `@2x` | 705×400 / 1410×800 | Neutral macOS DMG window backdrop |

## Social previews

GitHub's repository social preview ("Social preview" in repository Settings →
General) cannot be committed through repository files — it must be uploaded
through the web UI. `branding/social/` contains the ready-to-upload assets:

- `opengraph-1200x630.png` — 1200×630 (1.90476:1), the canonical OpenGraph shape.
- `github-social-1280x640.png` — 1280×640 (2:1), GitHub's preferred shape. The
  banner's proportions are preserved (never stretched); the difference is filled
  with transparent padding.

The banner is 1.9107:1, so the OpenGraph image is a near-perfect fit (1 px
transparent border top/bottom) while the 2:1 GitHub shape pads the sides.

## Notes on attribution

Only the MXM *application* identity is replaced. Legitimate references to LMMS
(licensing, copyright, contributor history, `.mmp`/`.mmpz` compatibility,
historical notes, and upstream URLs) are intentionally retained throughout the
source tree.
