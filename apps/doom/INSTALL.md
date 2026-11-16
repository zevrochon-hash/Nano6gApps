# Doom for iPod nano 7G (Freedoom Edition)

A playable port of **Freedoom E1M1** for the iPod nano 7th generation. It uses
the real Freedoom assets (compiled from the Freedoom source / WAD) — textures,
flats, sprites, HUD, title art, font and sounds — with a compact custom
Doom-style engine written from scratch (sector BSP, textured walls, floor and
ceiling flats, billboard sprites, dynamic lighting, E1M1's full line/sector
data and gameplay: former humans, imp & caco behaviour, doors, keys, items,
projectiles, damage flash, face status bar).

## What you need

1. NanoApps homebrew running on your iPod nano 7G (see the top-level README).
2. ~2.7 MB of free space on the iPod for `/Data/Doom`.

## Install

Put the iPod in file mode (`start eject`, or connect it like a USB drive) and
copy, keeping the folder structure exactly:

    Apps/Doom.nanoapp/          ->  Apps/Doom.nanoapp/
    Data/Doom/doom.pak          ->  Data/Doom/doom.pak
    Data/Doom/sfx/*.wav         ->  Data/Doom/sfx/*.wav

The ready-to-copy tree is at `build/deploy/` in this folder, and the packaged
app is at `NanoApps/build/packages/doom.zip`. Then eject the iPod and tap
**Doom** on the home screen (it appears automatically; no restart needed).

If you see a dark red screen: the app could not read `/Data/Doom/doom.pak` —
double-check the path and that the copy finished completely.

## Controls

| Input             | Action                         |
|-------------------|--------------------------------|
| Left-half drag    | strafe left/right, walk fwd/back (vertical) |
| Right-half drag   | turn left/right                |
| Tap               | fire                           |
| Play/Pause button | fire                           |
| Vol+              | fire                           |
| Vol-              | use / open doors / exit        |
| Tap the ‖ icon    | pause menu (resume / restart / main menu / **TILT AIM** toggle) |
| Pause → TILT AIM  | experimental tilt-to-look via the accelerometer (off by default) |

Hold the iPod in landscape with the home button on the right (like an
iPhone) — that's the orientation the game renders in, same as the Blocks
app's default.

In menus, tap a line to select it. In the demo, you start with the blue keycard,
a shotgun and 200 health — use Vol- to open keyed doors. Reach the exit switch
(special 11 line) to finish the level.

## Notes

- Game view is landscape 432x240 rendered then rotated onto the portrait
  240x432 panel (tap coordinates are mapped accordingly).
- The engine is fixed-point integer; no libm. E1M1 map data, all textures,
  flats, sprites, HUD gfx, font and sounds used in the level are embedded in
  `doom.pak` by `tools/convert_assets.py`.
- Freedoom is libre software: this port ships Freedoom assets under the
  Freedoom license. Source WADs/art: the `freedoom/` tree in the ipa folder
  and `/usr/share/games/doom/freedoom1.wad` on Linux.
- Old GL stub (doom.c/doom.h/doom_data.c) is kept for reference but is no
  longer built — the app is a RAW_SURFACE direct-draw app.

## Rebuild

    cd NanoApps/apps/doom
    python3 tools/convert_assets.py     # regenerate build/assets/doom.pak + sfx
    make                                # build build/doom.hbapp
    python3 -c "import sys; sys.path.insert(0,'../../tools'); from package_app import package_app; package_app('doom')"

`tools/preview` renders the same engine on your PC (title/menu/game screens
and a headless gameplay torture test) — see `tools/preview.c`.
