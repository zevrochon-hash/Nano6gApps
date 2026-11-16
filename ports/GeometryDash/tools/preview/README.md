# Geometry Dash offline preview

This harness runs the real `port/src/game.c` and `port/src/sprites.c` against a
small software implementation of the nano renderer (`soft_gfx.c`), so screens
and gameplay can be checked without a device.

- Resolution: 480 x 270 logical pixels, same layout as the app.
- Output: one PPM per scene (`01_menu` ... `10_settings_reset`).
- `GD_SIM=1` also runs an 1800-frame play loop to exercise movement,
  collisions, death/auto-restart, attempts and the save path.

## Build and run

```sh
make -C tools/preview            # build ./preview
make -C tools/preview shots      # write screenshots to tools/preview/shots/
make -C tools/preview sim        # run the gameplay simulation
```

Without a `shots` target you can call it directly:

```sh
tools/preview/preview data /tmp/gd-preview
GD_SIM=1 tools/preview/preview data /tmp/gd-preview
```

`data/` must contain the generated `sprites.bin`, `page00-07.bin`,
`font.bin`, `levels.bin` and `bg.bin`. Regenerate it with:

```sh
python3 tools/convert_assets.py   # sprites/pages, after editing source assets
python3 tools/convert_bg.py
python3 tools/convert_font.py
python3 tools/convert_levels.py
```

## Scenes

| File | Scene |
| --- | --- |
| `01_menu` | Main menu, Stereo Madness |
| `02_menu_practice` | Menu with practice on and another level |
| `03_settings` | Settings panel |
| `04_play_stereo` | Stereo Madness gameplay |
| `05_play_btrack` | Back on Track gameplay |
| `05b_play_chains` | Chains / rods |
| `05c_play_portal` | Gravity portal |
| `06_play_ship` | Ship mode |
| `07_paused` | Pause menu |
| `08_dead` | Death / explosion state |
| `09_complete` | Level complete screen |
| `10_settings_reset` | Settings reset confirmation |

The preview is a development aid only; it is not linked into the app.
