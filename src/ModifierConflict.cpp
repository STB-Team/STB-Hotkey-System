#include "ModifierConflict.h"

#include "KeyConflict.h"
#include "Localization.h"
#include "MessageBox.h"
#include "Settings.h"

#include <SimpleIni.hpp>

#include <array>
#include <string>
#include <vector>

namespace HKS::ModifierConflict
{
	namespace
	{
		constexpr auto kIniPath = L"Data/SKSE/Plugins/STB_HotkeySystem.ini";

		// Candidate modifier keys we offer. Keyboard DX scancodes; the modifier MUST be
		// a keyboard key (the capture path only treats keyboard == modifier).
		struct KeyOption
		{
			std::uint32_t code;
			const char*   name;
		};
		constexpr std::array kCandidates{
			KeyOption{ 0x1D, "Left Ctrl" },
			KeyOption{ 0x38, "Left Alt" },
			KeyOption{ 0x2A, "Left Shift" },
		};

		// Key the player presses to open the Favorites menu (keyboard, gameplay context).
		std::uint32_t FavoritesKey()
		{
			auto* controlMap = RE::ControlMap::GetSingleton();
			auto* userEvents = RE::UserEvents::GetSingleton();
			if (!controlMap || !userEvents) {
				return RE::ControlMap::kInvalid;
			}
			return controlMap->GetMappedKey(userEvents->favorites, RE::INPUT_DEVICE::kKeyboard);
		}

		void PersistModifier(std::uint32_t a_code)
		{
			Settings::SetAssignModifier(a_code);

			CSimpleIniA ini;
			ini.SetUnicode();
			ini.LoadFile(kIniPath);  // ignore failure: a missing file is created on save
			ini.SetLongValue("Assignment", "iModifierScanCode", static_cast<long>(a_code),
				"; Keyboard DX scancode held to assign hotkeys. Must NOT equal the Favorites-menu key.");
			const auto rc = ini.SaveFile(kIniPath);
			logger::info("modifier switched to 0x{:X} ({}), INI save rc={}", a_code, KeyConflict::KeyName(a_code), static_cast<int>(rc));
		}
	}

	void CheckAndPrompt()
	{
		static bool prompted = false;
		if (prompted) {
			return;
		}

		const std::uint32_t favKey = FavoritesKey();
		const std::uint32_t modKey = Settings::AssignModifier();

		// The group modifier has two hard requirements, and neither is worth a prompt --
		// there is a working default and only a hand-edited INI can break it. Sharing the
		// assign modifier makes "replace" and "stack" indistinguishable; sharing the
		// Favorites key brings back the menu that could not be closed. Either way groups
		// are switched off for the session rather than misbehaving.
		if (const std::uint32_t groupKey = Settings::GroupModifier(); groupKey != 0) {
			const char* why = groupKey == modKey ? "the assign modifier" :
			                  (groupKey == favKey ? "the Favorites-menu key" : nullptr);
			if (why) {
				logger::warn("group modifier 0x{:X} ({}) is also {} -- groups disabled; "
				             "set [Assignment] iGroupModifierScanCode to a free key",
					groupKey, KeyConflict::KeyName(groupKey), why);
				Settings::SetGroupModifier(0);
			}
		}
		if (favKey == RE::ControlMap::kInvalid || favKey != modKey) {
			return;  // no conflict
		}
		prompted = true;

		// Offer every candidate that isn't the (conflicting) Favorites key.
		std::vector<std::string>   buttons;
		std::vector<std::uint32_t> codes;
		for (const auto& c : kCandidates) {
			if (c.code != favKey) {
				buttons.emplace_back(Localization::Format("$STB_HK_ModifierConflict_Use", { c.name }));
				codes.push_back(c.code);
			}
		}
		buttons.emplace_back(Localization::Get("$STB_HK_ModifierConflict_Keep"));

		const std::string body = Localization::Format(
			"$STB_HK_ModifierConflict_Body", { KeyConflict::KeyName(modKey) });

		ShowMessageBox(body, [codes](unsigned int a_index) {
			if (a_index < codes.size()) {
				PersistModifier(codes[a_index]);
			} else {
				logger::info("modifier conflict: player chose to keep the shared key");
			}
		}, buttons);

		logger::info("modifier conflict prompt shown (modifier == favorites key 0x{:X})", modKey);
	}
}
