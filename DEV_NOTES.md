# STB Hotkey System — Dev Notes (handoff)

Handoff doc so a fresh chat can orient fast. Working dir: `C:\dev\STB Hotkey System`.
(There is also an auto-memory under `C:\Users\Sneyk\.claude\projects\C--dev-STB\memory\hotkey-system-project.md`,
but that is keyed to the *STB* project path and may not auto-load here — this file is the source of truth.)

## What this mod is
SKSE plugin: unlimited, fully-rebindable favorites hotkeys with **chord combos** (up to 2 keys),
**instance-precise** (tells an enchanted sword from a plain one of the same base), with **keycap icons**
rendered in the inventory/container/magic/favorites menus **without overwriting any SWF** (runtime SWF
injection). Bound items are auto-favorited (like vanilla F). Shouts/powers can equip+instantly cast on one press.

## Build / deploy
- CommonLibSSE-**NG** via vcpkg (port `commonlibsse-ng`, colorglass registry). NOT the fenix submodule
  (the old `.gitmodules`/`CLAUDE.md` are stale template leftovers). One DLL covers SE+AE.
- Build: `cmake --preset default -B build -S .` then `cmake --build build --config Release`.
- `/WX` everywhere EXCEPT vendored `src/swfhelper/*` (CMake sets `/WX-` on those 4 files).
- Post-build copies DLL+PDB to deploy dir. Deploy dir is a **CMake cache var** `STB_WIDGETS_DEPLOY_DIR`
  — change via `-D`, editing the default in CMakeLists does NOT override the cache.
- Deploy targets:
  - DLL+PDB → `C:\Skyrim\[STB] Mod Organizer\mods\STB Hotkey System\skse\plugins`
  - SWF → `...\STB Hotkey System\Interface\STB_Keycaps.swf`
  - INI → `...\skse\plugins\STB_HotkeySystem.ini`
- **PowerShell gotcha:** paths with `[STB]` are wildcards — use `-LiteralPath`.
- User plays on **SE 1.5.97** (but build is NG → SE+AE). User UI mod: **Untarnished** (a SkyUI fork).

## Runtime tooling
- FFDec CLI: `C:\Program Files (x86)\FFDec\ffdec-cli.exe` (swf↔xml, dumpAS2/dumpSWF, -replace).
- Keycap source SWF: `STB_Keycaps_src.swf` (built by exporting the vanilla "Keyboard" keycap clip chid 157
  as linkage `STBKeycap`). Made via swf2xml → regex-insert ExportAssets (UTF-8 **no BOM**) → xml2swf.
  Deployed to Interface as `STB_Keycaps.swf`.

## Architecture (src/)
- **main.cpp** — SKSEPlugin_Load: log, Serialization::Register, kDataLoaded installs:
  Settings::Load, FavoritesHook, InventoryIcons (LoadResources+Install), ShoutHook, InputHandler::Register.
- **Hotkey.h** — `Bind{device, keys[] (sorted chord)}`, `ItemId{form, ench, uid, health}`,
  `Hotkey{bind, id}`. `ReadIdentity(InventoryEntryData*)` pulls ench/uid/health from extra-data.
- **HotkeyManager** — owns `vector<Hotkey>`. Assign/RemoveByItem/FindByItem (exact ItemId), FindByForm
  (any instance, for favorites), ResolveChord (longest-match by device+held-set). Pure data, never mutates
  game inventory.
- **Serialization** — co-save v3: device, form(resolve), ench(resolve), uid, health, chord keys. Drops
  individual unresolved hotkeys, never fails the whole load. RevertCallback clears.
- **Settings** — INI `Data/SKSE/Plugins/STB_HotkeySystem.ini`, re-read on every menu open (live tuning).
  `[Assignment] iModifierScanCode` (default 29=LCtrl), `[Icons]/[IconsMagic]/[IconsFavorites]`
  fScale/fY/bAfterName/fX/fGap, `[Gameplay] bCastVoiceOnEquip`.
- **InputHandler** — global `BSTEventSink<InputEvent*>`: tracks per-device held-sets; **assignment capture**
  (hold modifier in an assign-menu → chord of up to 2 keys, keyboard OR mouse, single-device; commit on
  modifier release; target locked at modifier-press so SkyUI type-search can't move selection); **firing**
  for non-voice forms (voice handled by ShoutHook). `IsHeld`, `ResolveForKey` (used by ShoutHook).
- **MenuAssign** — maps each assign-menu to its GFx `selectedEntry` path; reads form + STBench/STBuid/
  STBhealth → ItemId. Favorites path differs from the shared InventoryLists path.
- **EquipDispatch** — Fire(ItemId) (deferred via SKSE task), EquipNow(ItemId) (sync, for ShoutHook),
  IsVoiceForm (shout or spell w/ voice slot 0x25BEE). EquipForm: spells (slots by FormID 0x13F42/43/45,
  two-handed by pointer-compare — never derefs spell's equip slot, that crashed on a custom spell), shouts
  (EquipShout), items (FindInstanceList picks the exact ExtraDataList by ench/uid/health; **worn-toggle
  tests the instance's kWorn/kWornLeft, not base form** — base-form check caused "empty hands" when
  switching between same-base instances).
- **InventoryIcons** — runtime keycap rendering, no SWF overwrite. PostCreate(IMenu vfunc 0x2) on
  Inventory/Container/Magic/Gift/Barter (+Favorites): injects STBKeycap (ImportData) and wraps the entry
  prototype's render fn (`InventoryListEntry.formatName` for inv-style; `FavoritesListEntry.setEntry` for
  favorites). ScaleformCallback (`GetScaleformInterface()->Register`) stamps STBench/STBuid/STBhealth per
  row from the real InventoryEntryData. PushKeycaps (AdvanceMovie vfunc 0x5, throttled ~15 frames or
  dirty) diff-stamps hotkeyKey1/2 by ItemId match. AttachKeycap → `gotoAndStop(scancode)`; **mouse buttons
  use frame +256, gamepad +266**. Keycap placed after the name width AND past any shown vanilla icons
  (bestIcon/favoriteIcon/poisonIcon/stolenIcon/enchIcon/readIcon, or equip/mainHand/offHand for fav).
- **FavoritesHook** — FavoritesMenu vtable: AdvanceMovie(0x5) PushBadges stamps hotkeyKey1/2 by FindByForm;
  MenuCanProcess(0x1)/MenuProcessButton(0x5) block vanilla assignment while modifier held + kill vanilla
  number-key assign; HandlerProcessButton(0x5) kills vanilla 1-8 firing except the "open favorites" key.
- **ShoutHook** — ShoutHandler::CanProcess(vfunc 0x1, VTABLE_ShoutHandler). For a voice-form hotkey:
  EquipNow synchronously then return true so vanilla ProcessButton casts it. Passes the WHOLE key lifecycle
  (down→held→up) to the handler so shout charge scales with hold time like the Shout key (g_active/g_key).
- **Favorites** — FavoriteSelectedItem (real entry via `ItemList::get_selected` REL::ID(50086), SE-only,
  + game list refresh sub `RELOCATION_ID(50099,51031)`); RefreshMagicMenu (`RELOCATION_ID(51223,52098)`,
  magicList from MagicMenu::GetRuntimeData().unk30); EnsureFavorited (by-form fallback, temp
  InventoryEntryData), IsFavorited. Un-favoriting via vanilla F → next press removes the binding.
- **swfhelper/** — ported from Dynamic-Inventory-Icon-Injector (`C:\dev\STB\ModHotkey`... actually
  `C:\dev\STB Hotkey System\Dynamic-Inventory-Icon-Injector`): ImportData (runtime SWF symbol injection),
  SWF/{TagFactory,SWFOutputStream,ActionGenerator}. RE/Offsets.Ext.h (GFx internal vtable offsets).

## Reference material (in repo)
- `Dynamic-Inventory-Icon-Injector/` — the icon-injection technique we ported.
- `Untarnished/` — user's UI mod unpacked (favoritesmenu.swf + scripts). We do NOT ship our edited
  favoritesmenu anymore (icons go through runtime injection instead).
- `Skyui/` — SkyUI inventory/magic/container/gift swfs (for inspecting AS entry fields).
- `Dynamic-Inventory-Icon-Injector` reference crash mod was `ExtendedHotkeySystem` (in `C:\dev\STB\ModHotkey`)
  — the crashy/resetting mod we replaced.

## Identity model (important)
Items are matched by `ItemId{form, ench, uid, health}` (NOT just base form):
- ench = ExtraEnchantment.enchantment FormID (player-enchanted = dynamic FF…), uid = ExtraUniqueID.uniqueID,
  health = ExtraHealth*100. Plain/fungible = all zero (matches any plain copy).
- Spells/shouts: uid 0, match by form. Favorites menu uses FindByForm (only the favorited instance is there).
- Co-save resolves form+ench via ResolveFormID (uid/health verbatim). FF dynamic enchants survive within a
  save's lineage; a big load-order change could orphan them (rare, acceptable).

## Current state — WORKS (user-confirmed)
Assign from inventory/container/magic/barter/gift/favorites (Ctrl+chord, combos, mouse side buttons),
keycap icons in all those menus, instance precision (enchanted vs plain), auto-favorite + live refresh,
un-favorite removes binding, equip/toggle incl. switching between same-base instances, shout/power
equip+instant-cast with hold-charge, spell equip crash fixed.

## Recent fixes (this session)
- **Barter/container merchant keycaps**: `InventoryIcons::PushKeycaps` now skips entries
  with `filterFlag >= 1024` (SkyUI tags player items < 1024, merchant/container stock >= 2048),
  so a fungible bind no longer paints a keycap on the vendor's same-base item.
- **Hotkeys firing in menus (dialogue etc.)**: `ShouldSuppressFiring` → public
  `InputHandler::FiringSuppressed()`, now menu-name based (DialogueMenu, Crafting, Book,
  Lockpicking, Sleep/Wait, Training, Tween, LevelUp, MessageBox, Console) so it survives
  **SkyrimSoulsRE** un-pausing menus. ShoutHook voice path now calls the same guard (it had none).
- **Modifier == Favorites-key conflict**: new `ModifierConflict` module + copied `MessageBox.h`
  (from `C:\dev\STB\include\MessageBox.h`). On kPostLoadGame/kNewGame, if the assign modifier
  scancode equals the Favorites-menu key, a message box offers a free modifier (LCtrl/LAlt/LShift)
  and rewrites `[Assignment] iModifierScanCode` in the INI (+ `Settings::SetAssignModifier`).
  Prompts once/session. The modifier is keyboard-only by design.
- **Vanilla-hotkey save migration**: new `VanillaMigration` module. On kPostLoadGame/kNewGame
  (after co-save load), adopts pre-existing vanilla favorites hotkeys into our system:
  item `ExtraHotkey` slots (player inventory) + `MagicFavorites::hotkeys[slot]`, mapping
  slot i -> number key (i+1) as a single-key bind, then clearing the vanilla slot (item kept
  favorited). Conservative: keeps our existing bind if the item/form already has one, and won't
  steal a number key already used in our system. Gated by `[Compatibility] bMigrateVanillaHotkeys`
  (default true). Idempotent (clears what it migrates).
- **Bind-commit timing option** `[Assignment] bEnableChords` (default true). `InputHandler`
  capture logic refactored into `CommitCapture()`. true: a lone key commits on modifier
  RELEASE (room to add a 2nd key), the 2nd key commits the chord instantly on press. false:
  single-key only, the first key press commits instantly (modifier + key down = bind).

## Known issues / TODO
- **Log spam**: `instance … ench …` (per enchanted/instanced row each refresh) and `equip fire …` are
  diagnostics — throttle or remove once stable.
- **Mouse wheel** (idCode 8/9) is momentary (no hold) — side buttons work, wheel may not in chords.
- **FavoritesHook PushBadges** doesn't apply the mouse +256 keycap offset (FindByForm path) — if a mouse
  bind's favorites badge looks wrong, mirror the offset logic from InventoryIcons::ChordScancodes.
- Stale `CLAUDE.md`/`.gitmodules` describe the old fenix/STB-template setup — ignore or clean up.

## Quick test loop
Edit `[Icons]` in the INI and just reopen the menu (no restart). Watching `STB_HotkeySystem.log` shows
settings, hooks, `instance …`, `assigned chord …`, `equip fire …`.
