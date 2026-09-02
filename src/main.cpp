#include <Windows.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

using namespace RE;
#include <SimpleIni.hpp>
#include <xbyak/xbyak.h>

#include "FavoritesHook.h"
#include "InputHandler.h"
#include "InventoryIcons.h"
#include "ModifierConflict.h"
#include "PickupWatch.h"
#include "Serialization.h"
#include "Settings.h"
#include "ShoutHook.h"
#include "VanillaMigration.h"

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		{
			HKS::Settings::Load();
			HKS::FavoritesHook::Install();
			HKS::InventoryIcons::LoadResources();
			HKS::InventoryIcons::Install();
			HKS::ShoutHook::Install();
			HKS::InputHandler::Register();
			HKS::PickupWatch::Register();
		}
		break;

	// A message box only displays once the player is in-game, so run the
	// modifier/Favorites-key conflict check on load (prompts at most once/session).
	case SKSE::MessagingInterface::kPostLoadGame:
	case SKSE::MessagingInterface::kNewGame:
		// Co-save binds are already loaded here, so ours win over any stale vanilla slot.
		HKS::VanillaMigration::Migrate();
		HKS::ModifierConflict::CheckAndPrompt();
		break;
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "STB_HotkeySystem";
	a_info->version = 1;

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	return true;
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;

	v.PluginVersion(1);
	v.PluginName("STB_HotkeySystem");
	v.AuthorName("STB");
	v.UsesAddressLibrary(true);
	v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST});
	v.HasNoStructUse(true);

	return v;
}();

void InitializeLog()
{
	auto path = logger::log_directory();
	if (!path) {
		stl::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%l] %v"s);
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	SKSE::Init(a_skse);
	SKSE::AllocTrampoline(1 << 11);

	HKS::Serialization::Register();

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", SKSEMessageHandler)) {
		return false;
	}

	return true;
}
