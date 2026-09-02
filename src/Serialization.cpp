#include "Serialization.h"

#include "HotkeyManager.h"

namespace HKS::Serialization
{
	namespace
	{
		template <class T>
		bool Write(SKSE::SerializationInterface* a_intfc, const T& a_value)
		{
			return a_intfc->WriteRecordData(&a_value, sizeof(T));
		}

		template <class T>
		bool Read(SKSE::SerializationInterface* a_intfc, T& a_out)
		{
			return a_intfc->ReadRecordData(&a_out, sizeof(T)) == sizeof(T);
		}
	}

	void SaveCallback(SKSE::SerializationInterface* a_intfc)
	{
		auto* mgr = HotkeyManager::GetSingleton();
		const auto& hotkeys = mgr->GetAll();

		if (!a_intfc->OpenRecord(kRecordHotkeys, kVersion)) {
			logger::error("failed to open HOTK record");
			return;
		}

		Write<std::uint32_t>(a_intfc, static_cast<std::uint32_t>(hotkeys.size()));

		for (const auto& h : hotkeys) {
			Write<std::uint32_t>(a_intfc, static_cast<std::uint32_t>(h.bind.device));
			Write<RE::FormID>(a_intfc, h.id.form);
			Write<RE::FormID>(a_intfc, h.id.ench);
			Write<std::uint16_t>(a_intfc, h.id.uid);
			Write<std::int32_t>(a_intfc, h.id.health);
			Write<std::uint32_t>(a_intfc, static_cast<std::uint32_t>(h.bind.keys.size()));
			for (auto k : h.bind.keys) {
				Write<std::uint32_t>(a_intfc, k);
			}
		}

		logger::info("saved {} hotkeys", hotkeys.size());
	}

	void LoadCallback(SKSE::SerializationInterface* a_intfc)
	{
		std::vector<Hotkey> loaded;

		std::uint32_t type;
		std::uint32_t version;
		std::uint32_t length;

		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type != kRecordHotkeys) {
				logger::warn("unknown co-save record {:08X}, skipping", type);
				continue;
			}
			if (version != kVersion) {
				logger::warn("HOTK version {} != {}, ignoring", version, kVersion);
				continue;
			}

			std::uint32_t count = 0;
			if (!Read(a_intfc, count)) {
				logger::error("failed reading hotkey count");
				break;
			}

			loaded.reserve(count);
			for (std::uint32_t i = 0; i < count; ++i) {
				std::uint32_t deviceRaw = 0;
				RE::FormID    savedForm = 0;
				RE::FormID    savedEnch = 0;
				std::uint16_t savedUid = 0;
				std::int32_t  savedHealth = 0;
				std::uint32_t nKeys = 0;

				if (!Read(a_intfc, deviceRaw) || !Read(a_intfc, savedForm) ||
					!Read(a_intfc, savedEnch) || !Read(a_intfc, savedUid) || !Read(a_intfc, savedHealth) ||
					!Read(a_intfc, nKeys)) {
					logger::error("truncated hotkey #{}", i);
					break;
				}

				Bind bind;
				bind.device = static_cast<RE::INPUT_DEVICE>(deviceRaw);
				bind.keys.reserve(nKeys);
				bool keysOk = true;
				for (std::uint32_t k = 0; k < nKeys; ++k) {
					std::uint32_t key = 0;
					if (!Read(a_intfc, key)) {
						keysOk = false;
						break;
					}
					bind.keys.push_back(key);
				}
				if (!keysOk) {
					logger::error("truncated chord on hotkey #{}", i);
					break;
				}
				bind.Canonicalize();

				// Resolve the form across the loaded plugin list. If the source mod
				// is gone or the id no longer resolves, drop just this hotkey -- never
				// fail the whole load (that is how the reference mod wiped everything).
				RE::FormID resolved = 0;
				if (!a_intfc->ResolveFormID(savedForm, resolved)) {
					logger::warn("could not resolve form {:08X}, dropping hotkey", savedForm);
					continue;
				}
				auto* form = RE::TESForm::LookupByID(resolved);
				if (!form) {
					logger::warn("form {:08X} not found, dropping hotkey", resolved);
					continue;
				}

				// The enchantment is a FormID too; resolve it across the load order. uid is
				// a per-save counter and health a fixed-point value -- kept verbatim.
				RE::FormID resolvedEnch = 0;
				if (savedEnch != 0) {
					a_intfc->ResolveFormID(savedEnch, resolvedEnch);
				}

				if (!bind.IsValid()) {
					continue;
				}

				loaded.push_back(Hotkey{ std::move(bind), ItemId{ resolved, resolvedEnch, savedUid, savedHealth } });
			}
		}

		HotkeyManager::GetSingleton()->ReplaceAll(std::move(loaded));
		logger::info("loaded {} hotkeys", HotkeyManager::GetSingleton()->GetAll().size());
	}

	void RevertCallback(SKSE::SerializationInterface*)
	{
		HotkeyManager::GetSingleton()->Clear();
	}

	void Register()
	{
		auto* ser = SKSE::GetSerializationInterface();
		ser->SetUniqueID(kUniqueID);
		ser->SetSaveCallback(SaveCallback);
		ser->SetLoadCallback(LoadCallback);
		ser->SetRevertCallback(RevertCallback);
	}
}
