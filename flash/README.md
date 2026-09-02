# Flash / SWF assets

The keycap graphic is imported into the game's item menus at runtime, so it has to be a
SWF that exports a single symbol named **`STBKeycap`** — a clip whose frame number equals
the DX scancode of the key (mouse buttons at frame +256, gamepad at +266, matching the
vanilla `SetHotkeyIcon` layout).

Because the keycap art is taken from whichever UI overhaul the user runs, there is one
folder per UI mod. Each folder holds only **our** sources; the third-party SWFs and the
assets extracted from them are deliberately **not** committed (see `.gitignore`) — grab
them from the UI mod yourself and build locally.

```
flash/
  Untarnished/   <- default; art taken from Untarnished UI
  SkyUI/         <- planned
```

## Building a keycaps SWF for a new UI mod

1. Make the folder: `flash/<UiMod>/`.
2. Take that mod's `favoritesmenu.swf` (it carries the keyboard keycap glyphs).
3. Find the glyph clip and give it the export name `STBKeycap`
   (JPEXS FFDec: right-click the sprite -> *Add to exports*).
4. Strip everything else out, so the file is a clean import source rather than a whole
   menu (see below).
5. Drop the result in the mod's `interface/STB_Keycaps.swf`.

Step 4 matters: an unstripped menu SWF drags in the entire UI mod's symbol table and its
SkyUI ActionScript classes. Those are exported assets inside a movie the game imports, and
they can shadow the running SkyUI classes. Use the helper:

```powershell
.\tools\strip_icon_swf.ps1 -In "favoritesmenu.swf" -Out "STB_Keycaps.swf" -Symbol STBKeycap
```

It keeps only the exported symbol plus its dependency closure, drops every other character,
export, script and external import, then verifies the symbol still renders.

## Why per-mod folders

The scancode -> frame mapping is fixed by the plugin ([`InventoryIcons.cpp`](../src/InventoryIcons.cpp)),
so a new SWF only changes how the keys *look*. It must keep:

- the export name `STBKeycap`;
- one frame per scancode, in the same order;
- a `stop()` on frame 1.
