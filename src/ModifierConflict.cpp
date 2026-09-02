#include "ModifierConflict.h"

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

		std::string KeyName(std::uint32_t a_code)
		{
			switch (a_code) {
			// Modifiers / control keys
			case 0x01: return "Esc";
			case 0x0E: return "Backspace";
			case 0x0F: return "Tab";
			case 0x1C: return "Enter";
			case 0x1D: return "Left Ctrl";
			case 0x9D: return "Right Ctrl";
			case 0x2A: return "Left Shift";
			case 0x36: return "Right Shift";
			case 0x38: return "Left Alt";
			case 0xB8: return "Right Alt";
			case 0x39: return "Space";
			case 0x3A: return "Caps Lock";
			// Number row
			case 0x02: return "1";
			case 0x03: return "2";
			case 0x04: return "3";
			case 0x05: return "4";
			case 0x06: return "5";
			case 0x07: return "6";
			case 0x08: return "7";
			case 0x09: return "8";
			case 0x0A: return "9";
			case 0x0B: return "0";
			case 0x0C: return "-";
			case 0x0D: return "=";
			// Letters (QWERTY rows)
			case 0x10: return "Q";
			case 0x11: return "W";
			case 0x12: return "E";
			case 0x13: return "R";
			case 0x14: return "T";
			case 0x15: return "Y";
			case 0x16: return "U";
			case 0x17: return "I";
			case 0x18: return "O";
			case 0x19: return "P";
			case 0x1A: return "[";
			case 0x1B: return "]";
			case 0x1E: return "A";
			case 0x1F: return "S";
			case 0x20: return "D";
			case 0x21: return "F";
			case 0x22: return "G";
			case 0x23: return "H";
			case 0x24: return "J";
			case 0x25: return "K";
			case 0x26: return "L";
			case 0x27: return ";";
			case 0x28: return "'";
			case 0x29: return "`";
			case 0x2B: return "\\";
			case 0x2C: return "Z";
			case 0x2D: return "X";
			case 0x2E: return "C";
			case 0x2F: return "V";
			case 0x30: return "B";
			case 0x31: return "N";
			case 0x32: return "M";
			case 0x33: return ",";
			case 0x34: return ".";
			case 0x35: return "/";
			// Function keys
			case 0x3B: return "F1";
			case 0x3C: return "F2";
			case 0x3D: return "F3";
			case 0x3E: return "F4";
			case 0x3F: return "F5";
			case 0x40: return "F6";
			case 0x41: return "F7";
			case 0x42: return "F8";
			case 0x43: return "F9";
			case 0x44: return "F10";
			case 0x57: return "F11";
			case 0x58: return "F12";
			default:
				return "клавиша (скан " + std::to_string(a_code) + ")";
			}
		}

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
			logger::info("modifier switched to 0x{:X} ({}), INI save rc={}", a_code, KeyName(a_code), static_cast<int>(rc));
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
		if (favKey == RE::ControlMap::kInvalid || favKey != modKey) {
			return;  // no conflict
		}
		prompted = true;

		// Offer every candidate that isn't the (conflicting) Favorites key.
		std::vector<std::string>   buttons;
		std::vector<std::uint32_t> codes;
		for (const auto& c : kCandidates) {
			if (c.code != favKey) {
				buttons.emplace_back(std::string("Назначить: ") + c.name);
				codes.push_back(c.code);
			}
		}
		buttons.emplace_back("Оставить как есть");

		const std::string body =
			"STB Hotkey System: клавиша-модификатор назначения хоткеев (" + KeyName(modKey) +
			") стоит на той же кнопке, что и открытие меню Избранного.\n\n"
			"На одну клавишу их ставить нельзя — назначение чорда и открытие/закрытие меню "
			"будут конфликтовать (меню глючит и не закрывается).\n\n"
			"Выбери другую клавишу-модификатор (будет сохранена в INI):";

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
