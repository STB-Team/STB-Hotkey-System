# Untarnished UI

Default keycap art. Source: `favoritesmenu.swf` from **Untarnished UI**.

- `symbols.csv` — symbol/export map of the source SWF, for reference when locating the
  keycap clip. In the stock file the keycap sprite is character **157**, and its glyphs are
  characters 559+ (147 vector shapes, no fonts or bitmaps).
- `scripts/__Packages/FavoritesListEntry.as` — our edited entry class, kept from an earlier
  approach that repacked `favoritesmenu.swf` to draw the hotkey badge. The shipped build no
  longer needs it: the badge is drawn from C++ by hooking `FavoritesListEntry.prototype.setEntry`
  ([`InventoryIcons.cpp`](../../src/InventoryIcons.cpp)). Kept for reference.

The built `STB_Keycaps.swf` is not committed — build it with `tools/strip_icon_swf.ps1`.
