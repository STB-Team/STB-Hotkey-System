# STB Hotkey System

An SKSE64 plugin that replaces Skyrim's favorites hotkeys with a chord-based system:
bind any item, spell, shout or power to a key (optionally a 2-key chord) from the
inventory, magic or favorites menu, and see the assigned key drawn on the list row.

Built for **Skyrim SE 1.5.97**.

## Why not the vanilla slots

Vanilla stores a hotkey as an `ExtraHotkey` slot index on the item's extra-data. The game
renumbers and clears that on stack splits, equips and container moves, which is why
hotkeys "reset". Here the chord is the key and the target form is the single source of
truth; nothing is stored on the item, and bindings live in the co-save.

## Features

- Chord binds (`modifier + key`, optional 2-key chords), assigned live from the menus.
- Keycap drawn on the inventory / magic / favorites rows, positioned after the item name.
- Instance-aware: an enchanted or tempered copy binds separately from a plain one.
- Shouts and powers can equip **and** cast on a single press.
- Bindings survive running out of a consumable — the star is restored when the item comes back.
- Optional migration of pre-existing vanilla hotkeys on first load.

## Building

Requires the `VCPKG_ROOT` environment variable, Visual Studio, and the vcpkg triplet
`x64-windows-static`.

```powershell
.\gen.ps1                       # configure (prompts for a deploy path)
cmake --build build --config Release
```

Dependencies come from vcpkg (`vcpkg.json`): CommonLibSSE-NG, spdlog, nlohmann_json, xbyak.
`COPY_BUILD=ON` copies the DLL/PDB to `STB_WIDGETS_DEPLOY_DIR` after a successful build.

The keycap graphic is a separate SWF, not part of the DLL. The default set is built from
SkyUI's button art; swapping the file restyles every keycap with no rebuild. See
[`flash/README.md`](flash/README.md) for the frame-layout contract and a step-by-step guide
to adapting it to another UI overhaul.

## Configuration

`Data/SKSE/Plugins/STB_HotkeySystem.ini` — a reference copy lives in
[`dist/SKSE/Plugins`](dist/SKSE/Plugins). Set `bDebugLog = 1` there to get verbose
diagnostics in `Documents/My Games/Skyrim Special Edition/SKSE/STB_HotkeySystem.log`.

## License

**GPL-3.0** — see [LICENSE](LICENSE).

The SWF import machinery in [`src/swfhelper/`](src/swfhelper) is adapted from
[Dynamic Inventory Icon Injector](https://github.com/JerryYOJ/Dynamic-Inventory-Icon-Injector-SKSE)
by **JerryYOJ**, which is GPL-3.0; that is why this project is GPL-3.0 as well. Those files
keep their attribution headers.

JerryYOJ was asked directly and granted permission for the reuse on 2026-09-04
("yep you can use that code, no worries"), which also settles the asset-use clause on the
DIII Nexus page. The GPL-3.0 terms are unchanged by that: this mod stays GPL-3.0 and its
source is published alongside the release.

## Credits

- **JerryYOJ** — Dynamic Inventory Icon Injector, the runtime SWF symbol injection.
- **SkyUI Team** — the default keycap art is built from SkyUI's `buttonart.swf`.
- The SKSE team and the CommonLibSSE-NG contributors.

## Not redistributed here

UI-overhaul SWFs and anything extracted from them belong to their authors, so no `.swf` is
committed — only the recipe to rebuild one. Reverse-engineering output is likewise kept out.
Build the keycaps SWF locally from the UI mod you actually have installed; see
[`flash/README.md`](flash/README.md), which also covers the per-mod permission situation.
