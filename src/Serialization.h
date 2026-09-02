#pragma once

namespace HKS::Serialization
{
	inline constexpr std::uint32_t kUniqueID = 'HKSY';  // co-save owner id
	inline constexpr std::uint32_t kRecordHotkeys = 'HOTK';
	inline constexpr std::uint32_t kVersion = 3;  // v3: instance identity (ench+uid+health)

	void Register();  // call once from SKSEPlugin_Load

	void SaveCallback(SKSE::SerializationInterface* a_intfc);
	void LoadCallback(SKSE::SerializationInterface* a_intfc);
	void RevertCallback(SKSE::SerializationInterface* a_intfc);
}
