#include "Settings.h"

#include <SimpleIni.hpp>

namespace HKS
{
	void Settings::Load()
	{
		constexpr auto path = L"Data/SKSE/Plugins/STB_HotkeySystem.ini";

		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(path) < 0) {
			logger::info("no INI found, using defaults (assign modifier = 0x{:X})", _assignModifier);
			return;
		}

		_assignModifier = static_cast<std::uint32_t>(
			ini.GetLongValue("Assignment", "iModifierScanCode", static_cast<long>(_assignModifier)));

		_groupModifier = static_cast<std::uint32_t>(
			ini.GetLongValue("Assignment", "iGroupModifierScanCode", static_cast<long>(_groupModifier)));

		_enableChords = ini.GetBoolValue("Assignment", "bEnableChords", _enableChords);

		_blockMenuKeys = ini.GetBoolValue("Assignment", "bBlockMenuKeys", _blockMenuKeys);

		_showAssignHint = ini.GetBoolValue("Assignment", "bShowAssignHint", _showAssignHint);

		_warnKeyConflict = ini.GetBoolValue("Assignment", "bWarnKeyConflict", _warnKeyConflict);

		_castVoiceOnEquip = ini.GetBoolValue("Gameplay", "bCastVoiceOnEquip", _castVoiceOnEquip);

		_migrateVanillaHotkeys = ini.GetBoolValue("Compatibility", "bMigrateVanillaHotkeys", _migrateVanillaHotkeys);

		_debugLog = ini.GetBoolValue("Debug", "bDebugLog", _debugLog);

		_iconAfterName = ini.GetBoolValue("Icons", "bAfterName", _iconAfterName);
		_iconScale = static_cast<float>(ini.GetDoubleValue("Icons", "fScale", _iconScale));
		_iconY = static_cast<float>(ini.GetDoubleValue("Icons", "fY", _iconY));
		_iconX = static_cast<float>(ini.GetDoubleValue("Icons", "fX", _iconX));
		_iconGap = static_cast<float>(ini.GetDoubleValue("Icons", "fGap", _iconGap));

		// Per-menu overrides (default to the inventory values if unset).
		_iconXMagic = static_cast<float>(ini.GetDoubleValue("IconsMagic", "fX", _iconX));
		_iconGapMagic = static_cast<float>(ini.GetDoubleValue("IconsMagic", "fGap", _iconGap));
		_iconXFav = static_cast<float>(ini.GetDoubleValue("IconsFavorites", "fX", _iconX));
		_iconGapFav = static_cast<float>(ini.GetDoubleValue("IconsFavorites", "fGap", _iconGap));

		logger::info("settings loaded: modifier=0x{:X} group=0x{:X} blockMenuKeys={} chords={} afterName={} scale={} Y={} X={}/{}/{} gap={}/{}/{}",
			_assignModifier, _groupModifier, _blockMenuKeys, _enableChords, _iconAfterName, _iconScale, _iconY,
			_iconX, _iconXMagic, _iconXFav, _iconGap, _iconGapMagic, _iconGapFav);
	}
}
