#pragma once

namespace HKS::Serialization
{
	inline constexpr std::uint32_t kUniqueID = 'HKSY';  // co-save owner id
	inline constexpr std::uint32_t kRecordHotkeys = 'HOTK';
	// v3: one item per chord, written as (device, form, ench, uid, health, keys).
	// v4: a chord holds a LIST of items (groups), written as (device, keys, items).
	// v3 records are still read so existing saves keep their hotkeys.
	inline constexpr std::uint32_t kVersion = 4;
	inline constexpr std::uint32_t kVersionSingleItem = 3;

	void Register();  // call once from SKSEPlugin_Load

	void SaveCallback(SKSE::SerializationInterface* a_intfc);
	void LoadCallback(SKSE::SerializationInterface* a_intfc);
	void RevertCallback(SKSE::SerializationInterface* a_intfc);
}
