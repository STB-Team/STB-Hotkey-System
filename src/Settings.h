#pragma once

namespace HKS
{
	// Minimal INI-backed config. Read once at kDataLoaded from
	// Data/SKSE/Plugins/STB_HotkeySystem.ini.
	class Settings
	{
	public:
		static void Load();

		// Keyboard scancode that puts the Favorites menu into "assign" mode while
		// held. Not part of the bound chord -- it only gates assignment. Default
		// 0x1D (Left Ctrl).
		static std::uint32_t AssignModifier() { return _assignModifier; }

		// Runtime override (used by the conflict prompt after it rewrites the INI), so
		// the new modifier takes effect without a reload.
		static void SetAssignModifier(std::uint32_t a_code) { _assignModifier = a_code; }

		// Second modifier: held instead of the assign one, a keypress ADDS the selected
		// item to whatever that key already holds, building a group that equips as a set
		// (SkyUI's item groups). Pressing it again on a member removes that member.
		// 0 disables groups entirely. Default 0x2A (Left Shift).
		static std::uint32_t GroupModifier() { return _groupModifier; }
		static void          SetGroupModifier(std::uint32_t a_code) { _groupModifier = a_code; }

		// While an assign modifier is held in an item menu, withhold menu keys from the
		// menu itself, so binding Ctrl+E doesn't also equip the item, Ctrl+R doesn't drop
		// it and Ctrl+F doesn't un-favorite it. The cancel control is always let through
		// so the menu stays closeable. See MenuInputBlock.
		static bool BlockMenuKeys() { return _blockMenuKeys; }

		// Allow 2-key chords when assigning.
		//   true  -> a single key commits on modifier RELEASE (so you can still add a 2nd
		//            key); a 2nd key commits the chord instantly on its press.
		//   false -> single-key only: the first key press commits instantly (modifier +
		//            key down = bind), no release needed.
		static bool EnableChords() { return _enableChords; }

		// When a hotkey equips a shout/power, also cast it immediately (via the
		// ShoutHandler), so one press equips AND fires it.
		static bool CastVoiceOnEquip() { return _castVoiceOnEquip; }

		// On loading a save, convert any pre-existing vanilla favorites hotkeys (item
		// ExtraHotkey slots + MagicFavorites hotkeys) into our binds on number keys 1-8,
		// then clear the vanilla slot. Lets old saves keep working with our system.
		static bool MigrateVanillaHotkeys() { return _migrateVanillaHotkeys; }

		// Warn (with a confirm prompt) when the key being bound is already a vanilla
		// gameplay control, so the player doesn't silently end up firing two things at
		// once. The vanilla Hotkey1..8 slots are never counted -- replacing those is the
		// whole point of this mod.
		static bool WarnKeyConflict() { return _warnKeyConflict; }

		// Verbose diagnostics (per-pickup, per-instance, "nothing held" notes). Off by
		// default -- those fire on ordinary play and would flood the log. Assignments,
		// binding removals and warnings are always logged regardless.
		static bool DebugLog() { return _debugLog; }

		// Keycap rendering (tweak live: edit the INI, reopen the menu). X and Gap are
		// per-menu kind (each list lays out differently); the rest is shared.
		enum class MenuKind
		{
			kInventory,
			kMagic,
			kFavorites
		};

		static bool  IconAfterName() { return _iconAfterName; }
		static float IconScale() { return _iconScale; }
		static float IconY() { return _iconY; }

		static float IconX(MenuKind a_kind)
		{
			switch (a_kind) {
			case MenuKind::kMagic:
				return _iconXMagic;
			case MenuKind::kFavorites:
				return _iconXFav;
			default:
				return _iconX;
			}
		}
		static float IconGap(MenuKind a_kind)
		{
			switch (a_kind) {
			case MenuKind::kMagic:
				return _iconGapMagic;
			case MenuKind::kFavorites:
				return _iconGapFav;
			default:
				return _iconGap;
			}
		}

	private:
		static inline std::uint32_t _assignModifier = 0x1D;
		static inline std::uint32_t _groupModifier = 0x2A;
		static inline bool          _blockMenuKeys = true;
		static inline bool          _enableChords = true;
		static inline bool          _castVoiceOnEquip = true;
		static inline bool          _migrateVanillaHotkeys = true;
		static inline bool          _warnKeyConflict = true;
		static inline bool          _debugLog = false;
		static inline bool          _iconAfterName = true;
		static inline float         _iconScale = 75.0f;
		static inline float         _iconY = 2.0f;
		static inline float         _iconX = 40.0f;
		static inline float         _iconGap = 20.0f;
		static inline float         _iconXMagic = 25.0f;
		static inline float         _iconGapMagic = 20.0f;
		static inline float         _iconXFav = 40.0f;
		static inline float         _iconGapFav = 20.0f;
	};
}
